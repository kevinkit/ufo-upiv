/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandre Lewkowicz (lewkow_a@epita.fr)
 */

#include <float.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-multi-search-task.h"
#include "ufo-ring-coordinates.h"

void* azimutal_wrapper(void* arg);

typedef struct _azimu_thread{


    int tid;

    UfoMultiSearchTaskPrivate *priv;
    UfoBuffer* image;
    UfoRingCoordinate *center;
    //Polynomials
    float *a;
    float *b;
    float *c;
} azimu_thread;

struct _UfoMultiSearchTaskPrivate {
    /* number of elements desired in computations of polynomial */

    cl_kernel found_cand;
    cl_context context;

    unsigned radii_range;
    float threshold;
    unsigned displacement;
};


static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMultiSearchTask, ufo_multi_search_task, UFO_TYPE_TASK_NODE,
        G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
            ufo_task_interface_init))

#define UFO_MULTI_SEARCH_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MULTI_SEARCH_TASK, UfoMultiSearchTaskPrivate))

enum {
    PROP_0,
    PROP_RADII_RANGE,
    PROP_THRESHOLD, 
    PROP_DISPLACEMENT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_multi_search_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_MULTI_SEARCH_TASK, NULL));
}

static void
ufo_multi_search_task_setup (UfoTask *task,
        UfoResources *resources,
        GError **error)
{
    UfoMultiSearchTaskPrivate *priv;
    priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE(task);

    priv->found_cand = ufo_resources_get_kernel(resources, "found_cand.cl", NULL, error);
    priv->context = ufo_resources_get_context(resources); 
    if(priv->found_cand)
    {
        UFO_RESOURCES_CHECK_CLERR(clRetainKernel(priv->found_cand));
    }

}

static void
ufo_multi_search_task_get_requisition (UfoTask *task,
        UfoBuffer **inputs,
        UfoRequisition *requisition)
{
    /* input[0] : contrasted image */
    /* input[1] : coordinate list */
    ufo_buffer_get_requisition (inputs[1], requisition);
}

static guint
ufo_multi_search_task_get_num_inputs (UfoTask *task)
{
    return 2;
}

static guint
ufo_multi_search_task_get_num_dimensions (UfoTask *task,
        guint input)
{
    return 1;
}

static UfoTaskMode
ufo_multi_search_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static int
min (int l, int r)
{
    if (l > r)
        return r;

    return l;
}

static int
max (int l, int r)
{
    if (l > r)
        return l;
    return r;
}

static void
get_coords(int *left, int *right, int *top, int *bot, int rad,
        UfoRequisition *req, UfoRingCoordinate *center)
{
    int l = (int) roundf (center->x - (float) rad);
    int r = (int) roundf (center->x + (float) rad);
    int t = (int) roundf (center->y - (float) rad);
    int b = (int) roundf (center->y + (float) rad);
    *left = max (l, 0);
    *right = min (r, (int) (req->dims[0]) - 1);
    // Bottom most point is req->dims[1]
    *top = max (t, 0);
    // Top most point is 0
    *bot = min (b, (int) (req->dims[1]) - 1);

}

/* Sum each pixel of ring from center with a radius of r */
static float
compute_intensity (UfoBuffer *ufo_image, UfoRingCoordinate *center, int r)
{
    float intensity = 0;
    UfoRequisition req;
    ufo_buffer_get_requisition(ufo_image, &req);
    float *image = ufo_buffer_get_host_array(ufo_image, NULL);
    int x_center = (int) roundf (center->x);
    int y_center = (int) roundf (center->y);
    int left, right, top, bot;
    get_coords(&left, &right, &top, &bot, r, &req, center);
    unsigned counter = 0;
    for (int y = top; y <= bot; ++y) {
        for (int x = left; x <= right; ++x) {
            int xx = (x - x_center) * (x - x_center);
            int yy = (y - y_center) * (y - y_center);
            /* Check if point is on ring */
            if (fabs (sqrtf ((float) (xx + yy)) - (float) r) < 0.5) {
                intensity += image[x + y * (int) req.dims[0]];
                ++counter;
            }
        }
    }
    return intensity / (float) counter;
}

/*  X_1^order ..... X^0
 *  |
 *  |
 *  X_i^order ..... X^0
 */
static float *
vandermonde_new (unsigned x, unsigned nb_elt, unsigned order)
{
    float *vandermonde = malloc (sizeof (float) * nb_elt * (order + 1));
    for (unsigned j = 0; j <= order; ++j) {
        for (unsigned i = 0; i < nb_elt; ++i) {
            vandermonde[i * (order + 1) + j] = powf ((float) (x + i),
                    (float) (order - j));
        }
    }
    return vandermonde;
}

/* Computes the projectios of vector A(:, j) over e */
static void
compute_projection (float *e, float *A, unsigned j, unsigned row,
        unsigned column, float *dst)
{
    float sc1 = 0;
    float sc2 = 0;
    /* Scalar product */
    for (unsigned i  = 0; i < row; ++i) {
        sc1 += e[i] * A[i * column + j];
        sc2 += e[i] * e[i];
    }

    /* Compute projection */
    for (unsigned i  = 0; i < row; ++i) {
        dst[i] = e[i] * sc1 / sc2;
    }
}

static float *
Gram_Schmidt_U(float *A, unsigned row, unsigned column)
{
    float *U = malloc (sizeof (float) * row * column);
    float *norms = malloc (sizeof (float) * column);
    float *proj_sum = malloc (sizeof (float) * row);
    float *e = malloc (sizeof (float) * row);
    float *proj = malloc (sizeof (float) * row);
    for (unsigned j = 0; j < column; ++j) {

        /* Compute the sum of projections of column j over each vector in the
         * orthonrmal basis */
        memset (proj_sum, 0, sizeof (float) * row);
        for (int k = 0; k < (int) j; ++k) {
            /* k represents a column */
            for (unsigned i = 0; i < row; ++i) {
                e[i] = U[i * column + (unsigned) k] / norms[k];
            }
            compute_projection (e, A, j, row, column, proj);
            for (unsigned i = 0; i < row; ++i) {
                proj_sum[i] += proj[i];
            }
        }

        /* compute column j of matrix U */
        for (unsigned i = 0; i < row; ++i) {
            U[i * column + j] = A[i * column + j] - proj_sum[i];
        }

        /* compute new norm of column j */
        norms[j] = 0;
        for (unsigned i = 0; i < row; ++i) {
            norms[j] += U[i * column + j] * U[i * column + j];
        }
        norms[j] = sqrtf (norms[j]);
    }

    free (norms);
    free (proj_sum);
    free (e);
    free (proj);

    return U;
}

static float *
Gram_Schmidt_Q (float *A, unsigned row, unsigned column)
{
    float *Q = malloc (sizeof (float) * row * column);
    float *U = Gram_Schmidt_U(A, row, column);
    for (unsigned j = 0; j < column; ++j) {
        float norm = 0;
        for (unsigned i = 0; i < row; ++i) {
            norm += U[i * column + j] * U[i * column + j];
        }
        norm = sqrtf (norm);
        for (unsigned i = 0; i < row; ++i) {
            Q[i * column + j] = U[i * column + j] / norm;
        }
    }
    free (U);
    return Q;
}
/* Transpose first matrix, and multiply to second matrix */
/* column_Q is the number of column in Q befor being transposed */
static float *
matrix_transpose_mul2(float *Q, float *A, unsigned column_Q,
        unsigned row, unsigned column_A)
{
    float *res = calloc (1, sizeof (float) * column_Q * column_A);
    for (unsigned i = 0; i < column_Q; ++i) {
        for (unsigned j = 0; j < column_A; ++j) {
            for (unsigned k = 0; k < row; ++k) {
                res[i * column_A + j] += Q[k * column_Q + i] * A[k * column_A + j];
            }
        }
    }
    return res;
}

/* Transpose first matrix, and multiply to second matrix */
static float *
matrix_transpose_mul(float *Q, float *A, unsigned row, unsigned column)
{
    float *res = calloc (1, sizeof (float) * column * column);
    for (unsigned i = 0; i < column; ++i) {
        for (unsigned j = 0; j < column; ++j) {
            for (unsigned k = 0; k < row; ++k) {
                res[i * column + j] += Q[k * column + i] * A[k * column + j];
            }
        }
    }
    return res;
}
/* P(r_min) = values[0] */
/* P(r_min + 1) = values[1] */
/* P(r_min + nb_elt - 1) = values[nb_elt - 1] */
/* The step is always of 1 */
static void
polyfit (float *values, unsigned nb_elt, unsigned r_min,
        float *a, float *b, float *c)
{
    /* We have as many column as the order + 1 of the polynomial */
    unsigned column = 3;
    /* Compute vandermonde matrix */
    float *vandermonde = vandermonde_new(r_min, nb_elt, 2);

    /* Compute V = QR using Gram-Schmidt method */
    float *Q = Gram_Schmidt_Q(vandermonde, nb_elt, column);
    float *R = matrix_transpose_mul (Q, vandermonde, nb_elt, column);
    /* Q' x Values */
    float *Qtxy = matrix_transpose_mul2 (Q, values, column, nb_elt, 1);

    *c = Qtxy[2] / R[2 * column + 2];
    *b = (Qtxy[1] - R[1 * column + 2] * *c) / R[1 * column + 1];
    *a = (Qtxy[0] - R[2] * *c - R[1] * *b) / R[0];

    free (vandermonde);
    free (Q);
    free(R);
    free(Qtxy);
}

/* From a given image, vary the radius size and compare the variation of the
 * intensity for each radii.  From these intensities compute a polynomial P(r)
 * where r represents a radius */
static void
create_profile_advanced (UfoMultiSearchTaskPrivate *priv, UfoBuffer *image,
        UfoRingCoordinate *center, float *a, float *b, float *c)
{
    unsigned min_rad = 1;
    if (center->r > priv->radii_range)
        min_rad = (unsigned) center->r - priv->radii_range;
    unsigned max_rad = (unsigned) center->r + priv->radii_range;
    /* The value associated to each radius */
    float values[max_rad - min_rad + 1];

    unsigned pic_idx = 0;
    values[pic_idx] = 0;
    float max_a = 0;
    int displacement = (int) priv->displacement;
    UfoRingCoordinate copy = *center;

    for (int x = -displacement; x <= displacement; ++x) {
        for (int y = -displacement; y <= displacement; ++y) {
            UfoRingCoordinate urc =
            { copy.x + (float) x, copy.y + (float) y, copy.r, 0.0f, 0.0f };
            for (unsigned r = 0; r <= max_rad - min_rad; ++r) {
                values[r] = compute_intensity (image, &urc, (int) (r + min_rad));
                if (values[r] > values[pic_idx]) {
                    pic_idx = r;
                }
            }

            polyfit (values, max_rad - min_rad + 1, min_rad, a, b, c);
     //       printf("INSIDE WRAPPER \t X=%f\tY=%f\t\tA=%f\tB=%f\tC=%f\n",urc.x,urc.y,a[0],b[0],c[0]);
            if (*a <= max_a) {
                center->x = urc.x;
                center->y = urc.y;
                center->r = -*b / (2.0f * *a);
                max_a = *a;
            }
        }
    }
}


static char
center_search (UfoMultiSearchTaskPrivate *priv, UfoBuffer *image,
        UfoRingCoordinate *src, UfoRingCoordinate *dst)
{
    float a, b, c;
    /* Compute polynomial aX^2 + bX + c */
    create_profile_advanced(priv, image, src, &a, &b, &c);
    /* When contrast is too low, we delete ring */
    /* A represents the steepness of the polynomial, the more steep i-e the more
     * negative a is, the more contrast we have.  The ideal function is a
     * dirac */
    if (a <= -priv->threshold) {
        *dst = *src;
        dst->contrast = a;
        return 1;
    }

    return 0;
}


void* azimutal_wrapper(void* arg)
{
    azimu_thread *data = (azimu_thread*) arg;

    create_profile_advanced(data->priv,data->image,data->center,data->a,data->b,data->c);

    pthread_exit(NULL);

}

static gboolean
ufo_multi_search_task_process (UfoTask *task,
        UfoBuffer **inputs,
        UfoBuffer *output,
        UfoRequisition *requisition)
{


    UfoMultiSearchTaskPrivate *priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE (task);


    URCS* src = (URCS*)ufo_buffer_get_host_array(inputs[1],NULL);
    
    unsigned counter_cpu = (unsigned) src->nb_elt;

    //Thread
    azimu_thread thr_data[counter_cpu];
    GThread* threads[counter_cpu];
    
    unsigned cnt_err = 0;

    float a_pol[counter_cpu];
    float b_pol[counter_cpu];
    float c_pol[counter_cpu];

    GError *err;
    
    //call a thread for each candidate
    for(unsigned g = 0; g < counter_cpu; g++)
    {
        thr_data[g].tid = g;
        thr_data[g].center = &src->coord[g]; //needs to have an connection!!
        thr_data[g].center->x = src->coord[g].x;
        thr_data[g].center->y = src->coord[g].y;
        thr_data[g].center->r = 2; //Whats the radius normally ?
        thr_data[g].a = &a_pol[g];
        thr_data[g].b = &b_pol[g];
        thr_data[g].c = &c_pol[g];        
        thr_data[g].priv = priv;
        thr_data[g].image = inputs[0];

        threads[g] = g_thread_try_new(NULL, azimutal_wrapper,&thr_data[g],&err);   

        if(threads[g] == NULL)
        {
            printf("Could not create thread %d, error code %d, trying again\n",g,err->code);
            g--;
            cnt_err++;
            if(cnt_err > 20)
            {
                printf("Caught in thread loop ... Stopping execution in\n");
                break;
            }
        }
        else
        {
            cnt_err = 0;
        }
    } 

    for(unsigned g = 0; g < counter_cpu;g++)
    {
        g_thread_join(threads[g]);
 //       printf("FITTING ON:\tX=%f\tY=%f\tA= %f\tB=%f\tC=%f\n",thr_data[g].center->x,thr_data[g].center->y,a_pol[g],b_pol[g],c_pol[g]);

    }




    URCS dst;


    unsigned cnt = 0;

    float x_array[counter_cpu];
    float y_array[counter_cpu];
    
    float* img = ufo_buffer_get_host_array(inputs[0],NULL);
    float rest_img[counter_cpu];
    dst.coord = (UfoRingCoordinate*) malloc(sizeof(UfoRingCoordinate) * counter_cpu);

    for(unsigned g = 0; g < counter_cpu;g++)
    {
        //Check if contrat is beneath
        if(thr_data[g].a[0] <= -priv->threshold)
        {
//            printf("CONTRAT WAS beneath %f\n", thr_data[g].a[0]);
        }
        else
        {
            if(g == 0)
            {
                x_array[cnt] = thr_data[g].center->x;
                y_array[cnt] = thr_data[g].center->y;

                UfoRingCoordinate URC_tmp = {thr_data[g].center->x,thr_data[g].center->y,thr_data[g].center->r,0.0f,0.0f};

                printf("X=%f\tY=%f\t\tA=%f\tB=%f\tC=%f\n", x_array[cnt],y_array[cnt], thr_data[g].a[0],thr_data[g].b[0],thr_data[g].c[0]);
                rest_img[cnt] = img[g]; 
                dst.coord[cnt] = URC_tmp;    
                cnt++;

            }
            else
            {
                //check if coordinate was set before
                for(unsigned i = 0; i < cnt;i++)
                {
                    if((x_array[i] == thr_data[g].center->x) && (y_array[i] == thr_data[g].center->y))
                    {
                        break;
                    }
                }

                x_array[cnt] = thr_data[g].center->x;
                y_array[cnt] = thr_data[g].center->y;
    
                rest_img[cnt] = img[g];


                printf("X=%f\tY=%f\t\tA=%f\tB=%f\tC=%f\n", x_array[cnt],y_array[cnt], thr_data[g].a[0],thr_data[g].b[0],thr_data[g].c[0]);
                printf("PEAK AT (%f,%f)\n",-thr_data[g].b[0]/(2*thr_data[g].a[0]),(4*thr_data[g].a[0]*thr_data[g].c[0] - thr_data[g].b[0]*thr_data[g].b[0])/(4*thr_data[g].a[0]));
                UfoRingCoordinate URC_tmp = {thr_data[g].center->x,thr_data[g].center->y,thr_data[g].center->r,0.0f,0.0f};
                dst.coord[cnt] = URC_tmp; 
                cnt++;
            }
        }   
    }    

    float mean = 0;
    float std  = 0;
    //compute mean
    for(unsigned g = 0; g < cnt; g++)
    {
        mean += rest_img[g];
    }

    mean = mean/cnt;

    for(unsigned g = 0; g < cnt; g++)
    {
        std += pow(rest_img[g] - mean,2);
    }


    std = sqrt(std/cnt);

    printf("MEAN = %f\tSTD = %f\tSNR = %f\n",mean,std,mean/std);
    

    dst.nb_elt = cnt;

    UfoRequisition new_req = {.dims[0] = 1 + (unsigned) dst.nb_elt * sizeof(UfoRingCoordinate)/sizeof(float), .n_dims = 1};
    
    ufo_buffer_resize(output,&new_req);
    
    float *res = ufo_buffer_get_host_array(output,NULL);
    
    memcpy(res,&dst,(unsigned) (dst.nb_elt) * sizeof(UfoRingCoordinate) + sizeof(float));
        
    free(dst.coord); 


    return TRUE;
}

static void
ufo_multi_search_task_set_property (GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec)
{
    UfoMultiSearchTaskPrivate *priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_RADII_RANGE:
            priv->radii_range = g_value_get_uint (value);
            break;
        case PROP_THRESHOLD:
            priv->threshold = g_value_get_float (value);
            break;
        case PROP_DISPLACEMENT:
            priv->displacement = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_multi_search_task_get_property (GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    UfoMultiSearchTaskPrivate *priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_RADII_RANGE:
            g_value_set_uint (value, priv->radii_range);
            break;
        case PROP_THRESHOLD:
            g_value_set_float (value, priv->threshold);
            break;
        case PROP_DISPLACEMENT:
            g_value_set_uint (value, priv->displacement);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_multi_search_task_finalize (GObject *object)
{


    UfoMultiSearchTaskPrivate *priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE (object);

    if(priv->found_cand)
    {
        UFO_RESOURCES_CHECK_CLERR(clReleaseKernel(priv->found_cand));
    }

    G_OBJECT_CLASS (ufo_multi_search_task_parent_class)->finalize (object);

}


static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_multi_search_task_setup;
    iface->get_num_inputs = ufo_multi_search_task_get_num_inputs;
    iface->get_num_dimensions = ufo_multi_search_task_get_num_dimensions;
    iface->get_mode = ufo_multi_search_task_get_mode;
    iface->get_requisition = ufo_multi_search_task_get_requisition;
    iface->process = ufo_multi_search_task_process;
}

static void
ufo_multi_search_task_class_init (UfoMultiSearchTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_multi_search_task_set_property;
    gobject_class->get_property = ufo_multi_search_task_get_property;
    gobject_class->finalize = ufo_multi_search_task_finalize;

    properties[PROP_RADII_RANGE] =
        g_param_spec_uint ("radii_range",
                "Gives the radius scanning range",
                "Gives the radius scanning range",
                0, G_MAXUINT, 3,
                G_PARAM_READWRITE);

    properties[PROP_THRESHOLD] =
        g_param_spec_float ("threshold",
                "Give minimum contrast a ring should have",
                "Give minimum contrast a ring should have",
                0, G_MAXFLOAT, 0.01f,
                G_PARAM_READWRITE);

    properties[PROP_DISPLACEMENT] =
        g_param_spec_uint ("displacement",
                "How much rings center can be displaced",
                "How much rings center can be displaced",
                0, G_MAXUINT, 2,
                G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoMultiSearchTaskPrivate));
}

static void
ufo_multi_search_task_init(UfoMultiSearchTask *self)
{
    self->priv = UFO_MULTI_SEARCH_TASK_GET_PRIVATE(self);
    self->priv->radii_range = 10;
    self->priv->threshold = 0.5f;
    self->priv->displacement = 1;
}
