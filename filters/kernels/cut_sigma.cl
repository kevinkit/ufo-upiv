__kernel void cut(__global float* in_mem, global float* out_mem, float sigma)
{
    int id = get_global_id(0);

    if(in_mem[id] > sigma)
    {
            out_mem[id] = 3*sigma;
    }
    if(in_mem[id] < sigma)
    {
        out_mem[id] = -3*sigma;
    }


}
