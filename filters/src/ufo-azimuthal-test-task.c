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
 */

#include <math.h>
#include <gsl/gsl_multifit_nlin.h>

#include "ufo-azimuthal-test-task.h"
#include "ufo-ring-coordinates.h"


struct _UfoAzimuthalTestTaskPrivate {
    guint radii_range;
    guint displacement;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoAzimuthalTestTask, ufo_azimuthal_test_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_AZIMUTHAL_TEST_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_AZIMUTHAL_TEST_TASK, UfoAzimuthalTestTaskPrivate))

enum {
    PROP_0,
    PROP_TEST,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_azimuthal_test_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_AZIMUTHAL_TEST_TASK, NULL));
}

static void
ufo_azimuthal_test_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{

}

static void
ufo_azimuthal_test_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[1], requisition);
}

static guint
ufo_azimuthal_test_task_get_num_inputs (UfoTask *task)
{
    return 2;
}

static guint
ufo_azimuthal_test_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return (input == 0) ?  2 : 1;
}

static UfoTaskMode
ufo_azimuthal_test_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR;
}

static int min (int l, int r)
{
    return (l > r) ? r : l;
}

static int max (int l, int r)
{
    return (l > r) ? l : r;
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

    /*return intensity;*/

    
    if(counter != 0)
        return intensity / (float) counter;
    else    
        return 0;
}

struct fitting_data {
    size_t n;
    float *y;
};

static int gaussian_f (const gsl_vector *x, void *data, gsl_vector *f)
{
    size_t n = ((struct fitting_data *) data)->n;
    float *y = ((struct fitting_data *) data)->y;

    float A = gsl_vector_get(x, 0);
    float mu = gsl_vector_get(x, 1);
    float sig = gsl_vector_get(x, 2);

    for (size_t i = 0; i < n; i++)
    {
        float Yi = A * exp ( - (i - mu) * (i - mu) / (2.0f * sig * sig));
        gsl_vector_set (f, i, Yi - y[i]);
    }

    return GSL_SUCCESS;
}

static int gaussian_df (const gsl_vector *x, void *data, gsl_matrix *J)
{
    size_t n = ((struct fitting_data *) data)->n;

    float A = gsl_vector_get(x, 0);
    float mu = gsl_vector_get(x, 1);
    float sig = gsl_vector_get(x, 2);

    for (size_t i = 0; i < n; i++)
    {
        double t = i;
        double e = exp ( - (t - mu)*(t - mu) / (2.0f*sig*sig) );
        gsl_matrix_set (J, i, 0, e);
        gsl_matrix_set (J, i, 1, A * e * (t-mu) / (sig*sig) );
        gsl_matrix_set (J, i, 2, A * e * (t-mu)*(t-mu) / (sig*sig*sig) );
    }
    return GSL_SUCCESS;
}

static int gaussian_fdf (const gsl_vector *x, void *data, gsl_vector *f, gsl_matrix *J)
{
    gaussian_f (x, data, f);
    gaussian_df (x, data, J);
    return GSL_SUCCESS;
}


typedef struct _gaussian_thread_data{
    int tid;
   
    UfoAzimuthalTestTaskPrivate *priv;
    UfoRingCoordinate* ring;
    UfoRingCoordinate* p_ring;
    UfoRingCoordinate* n_ring;
    UfoBuffer* ufo_image;
   
    short* binary_pic; //Matrix of done pixels
    int x_len;
    int y_len;

    int tmp_S;
    UfoRingCoordinate* winner;
   
   
    GMutex* mutex;

} gaussian_thread_data;


static int compare_candidates(UfoRingCoordinate* a, UfoRingCoordinate *b)
{
    if (abs(a->x - b->x) < 3 && abs(a->y - b->y) < 3) {  
          if (a->contrast > b->contrast)
            return 0;
    }
    else
    {
        return 1;
    }

}

static void gaussian_thread(gpointer data, gpointer user_data)
{
    
    //Thread copying
    gaussian_thread_data *parm = (gaussian_thread_data*) data;
    UfoRingCoordinate* ring = (UfoRingCoordinate*) parm->ring;
    UfoRingCoordinate* p_ring = (UfoRingCoordinate*) parm->p_ring;
    UfoRingCoordinate* n_ring = (UfoRingCoordinate*) parm->n_ring;
    short* tmp_pic = (short*) parm->binary_pic;
    UfoAzimuthalTestTaskPrivate* priv = (UfoAzimuthalTestTaskPrivate*) parm->priv;

    //GSL create
    const gsl_multifit_fdfsolver_type *T;
    gsl_multifit_fdfsolver *s;

    T = gsl_multifit_fdfsolver_lmsder;
    gsl_multifit_function_fdf f;
    int status;

    f.f = &gaussian_f;
    f.df = &gaussian_df;
    f.fdf = &gaussian_fdf;
    f.p = 3;

    int min_r = ring->r - priv->radii_range;
    int max_r = ring->r + priv->radii_range;
    min_r = (min_r < 1) ? 1:min_r;

    float histogram[max_r - min_r +1];

    double x_init[] = {10,ring->r,2};
    gsl_vector_view x = gsl_vector_view_array(x_init,3);
    s = gsl_multifit_fdfsolver_alloc(T,max_r - min_r +1,3);
    f.n = max_r - min_r + 1;
    
    for (int j = - (int) priv->displacement; j < (int) priv->displacement + 1; j++){
        for (int k = - (int) priv->displacement; k < (int) priv->displacement + 1; k++){
        int pos = (int) ring->x + k + (ring->y + j)* parm->x_len;
        if(tmp_pic[pos] == 1){
            continue;
        } 
        if(p_ring != NULL){
            if(compare_candidates(ring,p_ring) == 0)
                 continue;
        }
        if(n_ring != NULL){
            if(compare_candidates(ring,n_ring) == 0)
                continue;
        }
        tmp_pic[pos] = 1;        
        
        for(int r = min_r; r <= max_r; ++r){
            histogram[r - min_r] = compute_intensity(parm->ufo_image,ring,r);
        }
        
        struct fitting_data d = {max_r - min_r +1,histogram};
        f.params = &d;
        gsl_multifit_fdfsolver_set(s,&f,&x.vector);
        int iter = 0;
    
        do{
            iter++;
            status = gsl_multifit_fdfsolver_iterate(s);
            if(status) break;

            status = gsl_multifit_test_delta(s->dx,s->x,1e-4,1e-4);
        
        }while(status == GSL_CONTINUE && iter < 50);
            
        float tmp_A = (float) gsl_vector_get(s->x,0);
        float tmp_sig = (float) gsl_vector_get(s->x,2);

        if(fabs(tmp_A) > (float)0.01){
            float tmp_s  = tmp_A/tmp_sig;
        
        
            g_mutex_lock(parm->mutex);
            if(tmp_s > parm->tmp_S) 
            {
                parm->tmp_S = tmp_s;
                parm->winner->x = (int) ring->x;
                parm->winner->y = (int) ring->y;
                parm->winner->r = (int) ring->r;
            }
            g_mutex_unlock(parm->mutex);
        }

        }
    }
}

static gboolean
ufo_azimuthal_test_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoAzimuthalTestTaskPrivate *priv = UFO_AZIMUTHAL_TEST_TASK_GET_PRIVATE (task);
    GTimer *timer = g_timer_new();


    float *in_cpu = ufo_buffer_get_host_array (inputs[1], NULL);
    guint num_cand = (guint) in_cpu[0];
    UfoRingCoordinate *cand = (UfoRingCoordinate*) &in_cpu[1];

    const gsl_multifit_fdfsolver_type *T;
    gsl_multifit_fdfsolver *s;

    T = gsl_multifit_fdfsolver_lmsder;
    gsl_multifit_function_fdf f;
    int status;

    f.f = &gaussian_f;
    f.df = &gaussian_df;
    f.fdf = &gaussian_fdf;
    f.p = 3;

    float t1=0.0, t2=0.0;

    int x_len = 512;
    int y_len = 512;

    short* tmp_pic = g_malloc0(sizeof(short) * x_len *y_len);


    for (unsigned i = 0; i < num_cand; i++) {
        int min_r = cand[i].r - priv->radii_range;
        int max_r = cand[i].r + priv->radii_range;
        min_r = (min_r < 1) ? 1 : min_r;

        g_message ("************");
        g_message("ring %d %d %d", (int)cand[i].x, (int)cand[i].y, (int)cand[i].r);

        float histogram[max_r - min_r + 1];

        double x_init[] = {10, cand[i].r, 2};
        gsl_vector_view x = gsl_vector_view_array (x_init, 3);
        s = gsl_multifit_fdfsolver_alloc (T, max_r - min_r + 1, 3);
        f.n = max_r - min_r + 1;

        float magic_x = 0;
        float magic_y = 0;
        float magic_r = 0;
        int c_t = 0;
        float save = 0;
        float x_save,y_save,r_save;
        int cnt_new = 0;
        float A = 0;

        



        for (int j = - (int) priv->displacement; j < (int) priv->displacement + 1; j++){
            for (int k = - (int) priv->displacement; k < (int) priv->displacement + 1; k++){
                


                //check wether this position has been computed before
                int pos = (int) cand[i].x + k + (cand[i].y + j) *x_len; 
                if(tmp_pic[pos] == 1){
                    g_message("Stopped it\n"); 
                    continue;
                }

                if(i > 0)
                if(fabs(cand[i].x + k - cand[i-1].x +k) < 3 || fabs(cand[i].y+j - cand[i -1].y+j) < 3)
                {
                        //check if the contrast next to it is brigher
                        if(cand[i].contrast > cand[i -1].contrast)
                            continue;
                }


                tmp_pic[pos] = 1;
                   
                magic_x = magic_x + cand[i].x +j;
                magic_y = magic_y + cand[i].y +k;
                magic_r = magic_r + cand[i].r;

                UfoRingCoordinate ring;
                if((j <= (int) priv->displacement) && (k <= (int) priv->displacement)){
                    ring.x = cand[i].x +j;
                    ring.y = cand[i].y +k;
                    ring.r = cand[i].r;
                    ring.intensity = 0.0;
                    ring.contrast = cand[i].contrast;
                    printf("no magic\n j = %d, k=%d\n",j,k);
                }
                else{
                    ring.x = magic_x/c_t;
                    ring.y = magic_y/c_t;
                    ring.r = magic_r/c_t;
                    ring.intensity = 0.0;
                    ring.contrast = 0.0;
                    g_message("Magic one j = %d, k = %d\n X = %d, Y= %d, r = %f\n",j,k,(int) ring.x,(int) ring.y,ring.r);
                } 
                c_t++;
                g_timer_start(timer);
                for (int r = min_r; r <= max_r; ++r) {
                    histogram[r-min_r] = compute_intensity(inputs[0], &ring, r);
                }
                t1 += g_timer_elapsed(timer, NULL);

                struct fitting_data d = {max_r - min_r + 1, histogram};
                f.params = &d;
                gsl_multifit_fdfsolver_set(s, &f, &x.vector);
                int iter = 0;

                g_timer_start(timer);
                do {
                    iter++;
                    status = gsl_multifit_fdfsolver_iterate (s);
                    if (status) break;

                    status = gsl_multifit_test_delta (s->dx, s->x, 1e-4, 1e-4);
                } while (status == GSL_CONTINUE && iter < 50);
                t2 += g_timer_elapsed(timer, NULL);

                float tmp_A = (float) gsl_vector_get(s->x,0);
                float tmp_sig = (float) gsl_vector_get(s->x,2);

                //if the amplitude is higher than a certain threshhold check if it was the highest among the others
                if(fabs(tmp_A) > (float)A){
                    float tmp_s = tmp_A/tmp_sig;
                    A = tmp_A;
                    if(tmp_s > save){
                        save = tmp_s;
                        x_save = (int) ring.x;
                        y_save = (int) ring.y;
                        r_save = (int) ring.r;
                        cnt_new++;
                    }
                }
                else{
                    continue;
                }
            
                g_message(
                    "histogram = %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f",
                    histogram[0], histogram[1], histogram[2], histogram[3], histogram[4], 
                    histogram[5], histogram[6], histogram[7], histogram[8], histogram[9]); 

                g_message("X = %d Y = %d\n",(int) x_save,(int)y_save);
                g_message ("iter = %d", iter);
                g_message ("min_r = %d", min_r);
                g_message ("A   = %.5f", gsl_vector_get(s->x, 0));
                g_message ("mu  = %.5f", gsl_vector_get(s->x, 1));
                g_message ("sig = %.5f", fabs(gsl_vector_get(s->x, 2)));
                g_message ("x = %d y = %d A/sig = %.5f", (int)ring.x, (int)ring.y, gsl_vector_get(s->x, 0) / fabs(gsl_vector_get(s->x, 2)));
            }
        }
    
        if(cnt_new > 0){
            FILE *file;
            file = fopen("testme.txt","a+");
            if(file == NULL)
            {
                g_message("error \n");
            }
            fprintf(file,"%d\t%d\t%f\n",(int) x_save,(int) y_save,r_save); 
            fclose(file);
        }
    }
    g_free(tmp_pic);
    g_message ("process");
    g_message ("compute_intensity = %f", t1); 
    g_message ("fitting = %f", t2); 

    g_timer_destroy(timer);
    return TRUE;
}

static void
ufo_azimuthal_test_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoAzimuthalTestTaskPrivate *priv = UFO_AZIMUTHAL_TEST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_TEST:
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_azimuthal_test_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoAzimuthalTestTaskPrivate *priv = UFO_AZIMUTHAL_TEST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_TEST:
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_azimuthal_test_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_azimuthal_test_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_azimuthal_test_task_setup;
    iface->get_num_inputs = ufo_azimuthal_test_task_get_num_inputs;
    iface->get_num_dimensions = ufo_azimuthal_test_task_get_num_dimensions;
    iface->get_mode = ufo_azimuthal_test_task_get_mode;
    iface->get_requisition = ufo_azimuthal_test_task_get_requisition;
    iface->process = ufo_azimuthal_test_task_process;
}

static void
ufo_azimuthal_test_task_class_init (UfoAzimuthalTestTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_azimuthal_test_task_set_property;
    oclass->get_property = ufo_azimuthal_test_task_get_property;
    oclass->finalize = ufo_azimuthal_test_task_finalize;

    properties[PROP_TEST] =
        g_param_spec_string ("test",
            "Test property nick",
            "Test property description blurb",
            "",
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoAzimuthalTestTaskPrivate));
}

static void
ufo_azimuthal_test_task_init(UfoAzimuthalTestTask *self)
{
    self->priv = UFO_AZIMUTHAL_TEST_TASK_GET_PRIVATE(self);
    self->priv->radii_range = 5;
    self->priv->displacement = 1;
}
