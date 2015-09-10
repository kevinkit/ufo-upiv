const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

kernel void
likelihood (global float *output, 
            read_only image2d_t input, 
            constant int *mask,
            int maskSizeH, 
            int n,
            __local float *part_pic)
{
    const int2 pos = (int2) (get_global_id (0), get_global_id (1));

    unsigned loc_x = get_local_size(0);
    unsigned loc_y = get_local_size(1);
    
    unsigned glo_x = get_global_size(0);
    unsigned glo_y = get_global_size(1);


    unsigned pos_x = get_local_id(0);
    unsigned pos_y = get_local_id(1);
   
    //check global boundaries
    if(pos.x > loc_x && pos.y > loc_y && pos_x + pos.x < glo_x && pos_y + pos.y < glo_y)
    {
        //check local boundaries
        if((pos_x < 3* maskSizeH +1) && (pos_y < 3*maskSizeH))
        {



            //is done by every workgroup --> can be accessed by every workgroup --> workgroups size: 8*8
            //writing 4 pixels per thread
       

            /* FOR A WORKGROUP SIZE OF 8x8
            //copying 4 pixels in a row
            //copying 4 pixels in a column 
            //check if copying is nececessary
            if(4*pos_x < 2*maskSizeH && 4*pos_y < 2*maskSizeH)
            {
                for(unsigned g = 0; g < 4; g++)
                {
                    for(unsigned j = 0; j < 4; j++)
                    {
                        part_pic[4*pos_x + g + 4*loc_x*(pos_y + j)] = read_imagef(input,smp,pos + (int2)(4*pos_x + g, 4*pos_y+j)).x;
                    }
                }
            }
        
            */


            /* for a workgroup size of 32x32 */
            part_pic[pos_x + loc_x*pos_y] = read_imagef(input,smp,pos + (int2)(pos_x,pos_y)).x;

            barrier(CLK_LOCAL_MEM_FENCE);
        


            int idx;
            int count = 0;
            float f;
            float mean = 0.0;
            float std = 0.0;

            for (int a = -maskSizeH; a < maskSizeH + 1; a++) {
                for (int b = -maskSizeH; b < maskSizeH + 1; b++) {
                    //why does the mask start at 13 ?, shouldnt it start on 0 ?

                    if (mask[a+maskSizeH+(b+maskSizeH)*(maskSizeH*2+1)] == 1) {
                        count++;
                        //mean += read_imagef(input, smp, pos + (int2)(a,b)).x;
                        mean += part_pic[pos_x + loc_x * pos_y];
                    }
                }
            }

            mean /= count;

            for (int a = -maskSizeH; a < maskSizeH + 1; a++) {
                for (int b = -maskSizeH; b < maskSizeH + 1; b++) {
                    if (mask[a+maskSizeH+(b+maskSizeH)*(maskSizeH*2+1)] == 1) {
           //             f = read_imagef(input, smp, pos + (int2)(a,b)).x;
                        f = part_pic[pos_x + loc_x * pos_y];
           
           
                        std += (f - mean) * (f - mean);
                    }
                }
            }

            std = sqrt(std/count);
 
            f = read_imagef(input, smp, pos).x;
            idx = pos.x + pos.y*get_global_size(0) + n*get_global_size(0)*get_global_size(1);
            output[idx] = exp((f - mean)/std);
        }
    }
}

