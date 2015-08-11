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




#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif


#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "ufo-contrast-task.h"


#include <pthread.h>
#include <threads/contrast_task.h>

struct _UfoContrastTaskPrivate {

    cl_context context;
    cl_kernel cut_kernel;
    cl_float sigma_top;
    cl_float sigma_bottom;
    gboolean remove_high;


};


static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoContrastTask, ufo_contrast_task, UFO_TYPE_TASK_NODE,
        G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
            ufo_task_interface_init))

#define UFO_CONTRAST_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_CONTRAST_TASK, UfoContrastTaskPrivate))

    enum {
        PROP_0,
        PROP_SIGMA_TOP,
        PROP_SIGMA_BOTTOM,
        N_PROPERTIES
    };

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

    UfoNode *
ufo_contrast_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_CONTRAST_TASK, NULL));
}

    static void
ufo_contrast_task_setup (UfoTask *task,
        UfoResources *resources,
        GError **error)
{
    UfoContrastTaskPrivate* priv;
    priv = UFO_CONTRAST_TASK_GET_PRIVATE (task);
    priv->cut_kernel = ufo_resources_get_kernel(resources, "cut_sigma.cl",NULL,error); 
    priv->context = ufo_resources_get_context(resources);

    if(priv->cut_kernel)
    {
        UFO_RESOURCES_CHECK_CLERR(clRetainKernel(priv->cut_kernel));
    }


}

    static void
ufo_contrast_task_get_requisition (UfoTask *task,
        UfoBuffer **inputs,
        UfoRequisition *requisition)
{
    ufo_buffer_get_requisition(inputs[0], requisition);
}

    static guint
ufo_contrast_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

    static guint
ufo_contrast_task_get_num_dimensions (UfoTask *task,
        guint input)
{
    return 2;
}

    static UfoTaskMode
ufo_contrast_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}


    static void
imadjust (UfoBuffer *ufo_src, UfoBuffer *ufo_dst, double low, double high,
        double gamma, float new_high)
{
    UfoRequisition req;
    ufo_buffer_get_requisition (ufo_src, &req);
    float *src = ufo_buffer_get_host_array(ufo_src, NULL);
    float *dst = ufo_buffer_get_host_array(ufo_dst, NULL);

    for (unsigned j = 0; j < req.dims[1]; ++j) {
        for (unsigned i = 0; i < req.dims[0]; ++i) {
            if (src[i + j * req.dims[0]] >= high)
                dst[i + j * req.dims[0]] = new_high;
            else if (src[i + j * req.dims[0]] <= low)
                dst[i + j * req.dims[0]] = 0.0;
            else {
                double normalized = (src[i + j * req.dims[0]] - low) / (double) (high - low);
                double val = pow(normalized, gamma);
                dst[i + j * req.dims[0]] = (float) val;
            }
        }
    }
}


static int get_pos(int x,int y,int w)
{
    return x+y*w;
}


//Will return the middle IDs
//x = lines y = rows 
// x_0/y_0 OFFSET refering to the whole image 
//len_y = Lenght of one peace
//real_len_y = length of whole image
//how many times the image was split up

static int get_real_pos(int x_til,int y_til, int x_0, int y_0,int len_y,int real_len_y,int parts)
{
    int refpoint = get_pos((parts >> 2) * x_0, (parts >> 2) * y_0,real_len_y);
    return (refpoint + get_pos(x_til,y_til,parts*len_y));

}



    static gboolean
ufo_contrast_task_process (UfoTask *task,
        UfoBuffer **inputs,
        UfoBuffer *output,
        UfoRequisition *requisition)
{
    UfoContrastTaskPrivate *priv = UFO_CONTRAST_TASK_GET_PRIVATE (task);
    UfoRequisition input_req;
    ufo_buffer_get_requisition (inputs[0], &input_req);


    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;


    cl_mem in_mem_gpu;
    cl_mem out_mem_gpu;
    gsize mem_size_c;

    float mean = 0;
    float std = 0;
    float top_cut;
    float bottom_cut;
    float tmp_std;

    unsigned amount;
    unsigned real_amount;

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));



    gfloat* in_mem = ufo_buffer_get_host_array(inputs[0], NULL);
    in_mem_gpu = ufo_buffer_get_device_array(inputs[0],cmd_queue);

    out_mem_gpu = ufo_buffer_get_device_array(output,cmd_queue);



    
    //compute std derivation

    int cut = 32;

    unsigned size_x_com = input_req.dims[0];
    unsigned size_y_com = input_req.dims[1];

    unsigned size_x = size_x_com/cut;
    unsigned size_y = size_y_com/cut;


    real_amount = (input_req.dims[1] * input_req.dims[0]);
    amount = 4*size_x*size_y;


    for(unsigned k = 0; k < 100; k++)
    {
        //compute mean
        mean = 0;
        std = 0;

        for(unsigned i = 0; i < size_x*2; i++)
        {
            for(unsigned j = 0; j < size_y*2; j++)
            {
                mean += in_mem[get_real_pos(j,k,size_x,size_y,size_y,size_y_com,cut)];
           //     printf("in_mem[%d] = %f\t", get_real_pos(j,i,size_x,size_y,size_y,size_y_com,cut), in_mem[get_real_pos(j,k,size_x,size_y,size_y,size_y_com,cut)]);
                if(j % 4 == 0)
                {
            //        printf("\n");
                }


            }
         //   printf("\n");
            

        }


        mean /= amount;


        for(unsigned i = 0; i < size_x*2; i++)
        {
            for(unsigned j = 0; j < size_y*2; j++)
            {       
                std += pow(in_mem[get_real_pos(j,i,size_x,size_y,size_y,size_y_com,cut)] - mean,2);
            }      

        }



        std = sqrt((std/(amount)));



        if(std == 0)
        {
            break;
        }

        if(k == 0)
        {
            tmp_std = std;
        }
        else
        {
            if(fabs(tmp_std - std)/std < 0.05)
            { 
                printf("old = %f, new = %f\n", tmp_std, std);  
             break;
            }   
            else
            {
                                printf("old = %f, new = %f\n", tmp_std, std); 
                 tmp_std = std;
            }
        }

        top_cut = mean + priv->sigma_top * std;
        bottom_cut = mean - priv->sigma_bottom * std;
        printf("Top cut = %f \t bottom cut = %f \t \n mean = %f \tstd-der = %f\n", top_cut , bottom_cut, mean, std);
        
        for(unsigned i = 0; i < size_x*2; i++)
        {
            for(unsigned j = 0; j < size_y*2; j++)
            {
                if(in_mem[get_real_pos(j,i,size_x,size_y,size_y,size_y_com,cut)] > top_cut && in_mem[get_real_pos(j,i,size_x,size_y,size_y,size_y_com,cut)] < bottom_cut)
                {
                    in_mem[get_real_pos(j,i,size_x,size_y,size_y,size_y_com,cut)] = 0;
                }
                
            }

        }


   
    }


    
    mem_size_c = (gsize) real_amount;


    UFO_RESOURCES_CHECK_CLERR(clSetKernelArg(priv->cut_kernel,0,sizeof(cl_mem), &in_mem_gpu));
    UFO_RESOURCES_CHECK_CLERR(clSetKernelArg(priv->cut_kernel,1,sizeof(cl_mem), &out_mem_gpu));
    UFO_RESOURCES_CHECK_CLERR(clSetKernelArg(priv->cut_kernel,2,sizeof(cl_float), &top_cut));
    UFO_RESOURCES_CHECK_CLERR(clSetKernelArg(priv->cut_kernel,3,sizeof(cl_float), &bottom_cut));


    ufo_profiler_call(profiler,cmd_queue, priv->cut_kernel,1,&mem_size_c,NULL);



    return TRUE;
}

    static void
ufo_contrast_task_set_property (GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec)
{
    UfoContrastTaskPrivate *priv = UFO_CONTRAST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SIGMA_TOP:
            priv->sigma_top = g_value_get_float(value);
            break;


        case PROP_SIGMA_BOTTOM:
            priv->sigma_bottom = g_value_get_float(value);
            break;   
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

    static void
ufo_contrast_task_get_property (GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec)
{
    UfoContrastTaskPrivate *priv = UFO_CONTRAST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SIGMA_TOP:
            g_value_set_float(value, priv->sigma_top);
            break;

        case PROP_SIGMA_BOTTOM:
            g_value_set_float(value,priv->sigma_bottom);

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

    static void
ufo_contrast_task_finalize (GObject *object)
{
    UfoContrastTaskPrivate *priv;
    priv = UFO_CONTRAST_TASK_GET_PRIVATE(object);

    if(priv->cut_kernel)
    {
        UFO_RESOURCES_CHECK_CLERR(clReleaseKernel(priv->cut_kernel));
    }


    G_OBJECT_CLASS (ufo_contrast_task_parent_class)->finalize (object);
}

    static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_contrast_task_setup;
    iface->get_num_inputs = ufo_contrast_task_get_num_inputs;
    iface->get_num_dimensions = ufo_contrast_task_get_num_dimensions;
    iface->get_mode = ufo_contrast_task_get_mode;
    iface->get_requisition = ufo_contrast_task_get_requisition;
    iface->process = ufo_contrast_task_process;
}

    static void
ufo_contrast_task_class_init (UfoContrastTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_contrast_task_set_property;
    gobject_class->get_property = ufo_contrast_task_get_property;
    gobject_class->finalize = ufo_contrast_task_finalize;

    properties[PROP_SIGMA_TOP] =
        g_param_spec_float ("sigma_top",
                "defines the window size, thus the threshold",
                "defines the widnow size, thus the threshold",
                1, G_MAXFLOAT, 5,
                G_PARAM_READWRITE);

    properties[PROP_SIGMA_BOTTOM] =
        g_param_spec_float ("sigma_bottom",
                "defines the window size, thus the threshold",
                "defines the widnow size, thus the threshold",
                1, G_MAXFLOAT, 5,
                G_PARAM_READWRITE);




    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoContrastTaskPrivate));
}

    static void
ufo_contrast_task_init(UfoContrastTask *self)
{
    self->priv = UFO_CONTRAST_TASK_GET_PRIVATE(self);
    self->priv->remove_high = 0;
}
