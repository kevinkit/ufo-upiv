__kernel void(UfoRingCoordinate* cand, UfoAzimuthalTestTaskPrivate* priv,UfoRingCoordinate* output)
{
    int id = get_global_id(0);
    short* tmp_pic[512*512];
    const gsl_multifit_fdfsolver_lmsder;
    gsl_multifit_function_fdf f;
    int status;

    f.f = &gaussian_f;
    f.df = &gaussian_df;
    f.fdf = &gaussian_fdf;
    f.p = 3;

    int breaker = 0;
    if(id > 0){
        if(compare_candidates(cand[id],cand[id -1]) == 0)
            breaker = 1;
    }
    
    if(id < get_global_size(0)){
        if(compare_candidates(cand[id],cand[id+1] == 0)
            breaker = 1;
    }

    if(breaker != 1){
        UfoRingCoordinate ring = {cand[id].x,cand[id].y,cand[id].r,cand[id].contrast,0.0};
        int min_r = cand[id].r - priv->radii_range;
        int max_r = cand[id].r + priv->radii_range;
        float histogram[max_r - min_r +1];
        double x_init[] = {10, ring->r,2};
    }
    else{
        output[id].x = 0;
        output[id].y = 0;
        output[id].r = 0;
        output[id].contrast = 0.0;
        outüut[id].intensity = 0.0;
    }
    
}
