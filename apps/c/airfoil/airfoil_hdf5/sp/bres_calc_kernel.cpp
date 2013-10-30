//
// auto-generated by op2.py on 2013-10-29 14:09
//

//user function
#ifndef BEG
#define BEG 1
#endif
#include "bres_calc.h"
inline void bres_calc_vec(floatv *x1,  floatv *x2,  floatv *q1,
                      floatv *adt1,floatv *res1,intv *bound) {
  floatv dx,dy,mu, ri, p1,vol1, p2,vol2, f;

  dx = x1[0] - x2[0];
  dy = x1[1] - x2[1];

  ri = 1.0f/q1[0];
  p1 = gm1*(q1[3]-0.5f*ri*(q1[1]*q1[1]+q1[2]*q1[2]));

  vol1 =  ri*(q1[1]*dy - q1[2]*dx);

  ri   = 1.0f/qinf[0];
  p2   = gm1*(qinf[3]-0.5f*ri*(qinf[1]*qinf[1]+qinf[2]*qinf[2]));
  vol2 =  ri*(qinf[1]*dy - qinf[2]*dx);

  mu = (*adt1)*eps;

  f = 0.5f*(vol1* q1[0]         + vol2* qinf[0]        ) + mu*(q1[0]-qinf[0]);
  res1[0] += select(*bound==1,0.0f,f);
  f = 0.5f*(vol1* q1[1] + p1*dy + vol2* qinf[1] + p2*dy) + mu*(q1[1]-qinf[1]);
  res1[1] += select(*bound==1,p1*dy,f);
  f = 0.5f*(vol1* q1[2] - p1*dx + vol2* qinf[2] - p2*dx) + mu*(q1[2]-qinf[2]);
  res1[2] += select(*bound==1,-p1*dx,f);
  f = 0.5f*(vol1*(q1[3]+p1)     + vol2*(qinf[3]+p2)    ) + mu*(q1[3]-qinf[3]);
  res1[3] += select(*bound==1,0.0f,f);
}

// host stub function
void op_par_loop_bres_calc(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1,
  op_arg arg2,
  op_arg arg3,
  op_arg arg4,
  op_arg arg5){

  int nargs = 6;
  op_arg args[6];

  args[0] = arg0;
  args[1] = arg1;
  args[2] = arg2;
  args[3] = arg3;
  args[4] = arg4;
  args[5] = arg5;
  int  ninds   = 4;
  int  inds[6] = {0,0,1,2,3,-1};

  #ifdef OP_PART_SIZE_3
    int part_size = OP_PART_SIZE_3;
  #else
    int part_size = OP_part_size;
  #endif

  // set number of threads
  #ifdef _OPENMP
    int nthreads = omp_get_max_threads();
  #else
    int nthreads = 1;
  #endif

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(3);
  op_timers_core(&cpu_t1, &wall_t1);


  int exec_size = op_mpi_halo_exchanges(set, nargs, args);
  int set_size = ((set->size+set->exec_size-1)/16+1)*16; //align to 512 bits
  op_plan *Plan = op_plan_get(name,set,part_size,nargs,args,ninds,inds);
  if (OP_diags>2) {
    printf(" kernel routine with indirection: bres_calc\n");
  }

  if (exec_size >0) {

    // execute plan
    int block_offset = 0;
    for ( int col=0; col<Plan->ncolors; col++ ){
      if (col==Plan->ncolors_core) {
        op_mpi_wait_all(nargs, args);
      }
      int nblocks = Plan->ncolblk[col];

      #pragma omp parallel for
      for ( int blockIdx=0; blockIdx<nblocks; blockIdx++ ){
        int blockId  = Plan->blkmap[blockIdx + block_offset];
        int nelem    = Plan->nelems[blockId];
        int offset_b = Plan->offset[blockId];
        //assumes blocksize is multiple of vector size
        int presweep = min(((offset_b-1)/16+1)*16,offset_b+nelem);
        for ( int n=offset_b; n<presweep; n++ ){
          int map0idx = arg0.map_data[n * arg0.map->dim + 0];
          int map1idx = arg0.map_data[n * arg0.map->dim + 1];
          int map2idx = arg2.map_data[n * arg2.map->dim + 0];

          bres_calc(
            &((float*)arg0.data)[2 * map0idx],
            &((float*)arg0.data)[2 * map1idx],
            &((float*)arg2.data)[4 * map2idx],
            &((float*)arg3.data)[1 * map2idx],
            &((float*)arg4.data)[4 * map2idx],
            &((int*)arg5.data)[1 * n]);
        }
        for ( int n=presweep/16; n<(offset_b+nelem)/16; n++ ){
          intv map0idx(&arg0.map_data_d[16*n + set_size * 0]);
          intv map1idx(&arg0.map_data_d[16*n + set_size * 1]);
          intv map2idx(&arg2.map_data_d[16*n + set_size * 0]);

          intv mapidx;
          mapidx = 2*map0idx;
          floatv arg0_p[2] = {
            floatv((float*)arg0.data+0, mapidx),
            floatv((float*)arg0.data+1, mapidx)};
          mapidx = 2*map1idx;
          floatv arg1_p[2] = {
            floatv((float*)arg1.data+0, mapidx),
            floatv((float*)arg1.data+1, mapidx)};
          mapidx = 4*map2idx;
          floatv arg2_p[4] = {
            floatv((float*)arg2.data+0, mapidx),
            floatv((float*)arg2.data+1, mapidx),
            floatv((float*)arg2.data+2, mapidx),
            floatv((float*)arg2.data+3, mapidx)};
          floatv arg3_p[1] = {
            floatv((float*)arg3.data+0, map2idx)};
          floatv arg4_p[4] = {
            floatv(0.0f),
            floatv(0.0f),
            floatv(0.0f),
            floatv(0.0f)};
          intv arg5_p[1] = {intv(&((int*)arg5.data)[16 * n])};
          bres_calc_vec(
            arg0_p,
            arg1_p,
            arg2_p,
            arg3_p,
            arg4_p,
            arg5_p);
          mapidx = 4*map2idx;
          store_scatter_add(arg4_p[0], (float*)arg4.data+0, mapidx);
          store_scatter_add(arg4_p[1], (float*)arg4.data+1, mapidx);
          store_scatter_add(arg4_p[2], (float*)arg4.data+2, mapidx);
          store_scatter_add(arg4_p[3], (float*)arg4.data+3, mapidx);
        }
        int postsweep = max(((offset_b+nelem)/16)*16, presweep);
        for ( int n=postsweep; n<offset_b+nelem; n++ ){
          int map0idx = arg0.map_data[n * arg0.map->dim + 0];
          int map1idx = arg0.map_data[n * arg0.map->dim + 1];
          int map2idx = arg2.map_data[n * arg2.map->dim + 0];

          bres_calc(
            &((float*)arg0.data)[2 * map0idx],
            &((float*)arg0.data)[2 * map1idx],
            &((float*)arg2.data)[4 * map2idx],
            &((float*)arg3.data)[1 * map2idx],
            &((float*)arg4.data)[4 * map2idx],
            &((int*)arg5.data)[1 * n]);
        }
      }
      block_offset += nblocks;
    }
  }

  if (exec_size == 0 || exec_size == set->core_size) {
    op_mpi_wait_all(nargs, args);
  }
  // combine reduction data
  op_mpi_set_dirtybit(nargs, args);

  // update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[3].name      = name;
  OP_kernels[3].count    += 1;
  OP_kernels[3].time     += wall_t2 - wall_t1;
}
