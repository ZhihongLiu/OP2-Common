//
// auto-generated by op2.m on 13-Mar-2012 15:24:26
//

// user function

#include "res_calc.h"


// x86 kernel function

void op_x86_res_calc(
  int    blockIdx,
  double *ind_arg0, int *ind_arg0_maps,
  double *ind_arg1, int *ind_arg1_maps,
  double *ind_arg2, int *ind_arg2_maps,
  double *ind_arg3, int *ind_arg3_maps,
  short *arg0_maps,
  short *arg1_maps,
  short *arg2_maps,
  short *arg3_maps,
  short *arg4_maps,
  short *arg5_maps,
  short *arg6_maps,
  short *arg7_maps,
  int   *ind_arg_sizes,
  int   *ind_arg_offs,
  int    block_offset,
  int   *blkmap,
  int   *offset,
  int   *nelems,
  int   *ncolors,
  int   *colors) {

  double arg6_l[4];
  double arg7_l[4];

  int   *ind_arg0_map, ind_arg0_size;
  int   *ind_arg1_map, ind_arg1_size;
  int   *ind_arg2_map, ind_arg2_size;
  int   *ind_arg3_map, ind_arg3_size;
  double *ind_arg0_s;
  double *ind_arg1_s;
  double *ind_arg2_s;
  double *ind_arg3_s;
  int    nelem, offset_b;

  char shared[128000];

  if (0==0) {

    // get sizes and shift pointers and direct-mapped data

    int blockId = blkmap[blockIdx + block_offset];
    nelem    = nelems[blockId];
    offset_b = offset[blockId];

    ind_arg0_size = ind_arg_sizes[0+blockId*4];
    ind_arg1_size = ind_arg_sizes[1+blockId*4];
    ind_arg2_size = ind_arg_sizes[2+blockId*4];
    ind_arg3_size = ind_arg_sizes[3+blockId*4];

    ind_arg0_map = ind_arg0_maps + ind_arg_offs[0+blockId*4];
    ind_arg1_map = ind_arg1_maps + ind_arg_offs[1+blockId*4];
    ind_arg2_map = ind_arg2_maps + ind_arg_offs[2+blockId*4];
    ind_arg3_map = ind_arg3_maps + ind_arg_offs[3+blockId*4];

    // set shared memory pointers

    int nbytes = 0;
    ind_arg0_s = (double *) &shared[nbytes];
    nbytes    += ROUND_UP(ind_arg0_size*sizeof(double)*2);
    ind_arg1_s = (double *) &shared[nbytes];
    nbytes    += ROUND_UP(ind_arg1_size*sizeof(double)*4);
    ind_arg2_s = (double *) &shared[nbytes];
    nbytes    += ROUND_UP(ind_arg2_size*sizeof(double)*1);
    ind_arg3_s = (double *) &shared[nbytes];
  }

  // copy indirect datasets into shared memory or zero increment

  for (int n=0; n<ind_arg0_size; n++)
    for (int d=0; d<2; d++)
      ind_arg0_s[d+n*2] = ind_arg0[d+ind_arg0_map[n]*2];

  for (int n=0; n<ind_arg1_size; n++)
    for (int d=0; d<4; d++)
      ind_arg1_s[d+n*4] = ind_arg1[d+ind_arg1_map[n]*4];

  for (int n=0; n<ind_arg2_size; n++)
    for (int d=0; d<1; d++)
      ind_arg2_s[d+n*1] = ind_arg2[d+ind_arg2_map[n]*1];

  for (int n=0; n<ind_arg3_size; n++)
    for (int d=0; d<4; d++)
      ind_arg3_s[d+n*4] = ZERO_double;


  // process set elements

  for (int n=0; n<nelem; n++) {

    // initialise local variables

    for (int d=0; d<4; d++)
      arg6_l[d] = ZERO_double;
    for (int d=0; d<4; d++)
      arg7_l[d] = ZERO_double;

    // user-supplied kernel call

    res_calc( ind_arg0_s+arg0_maps[n+offset_b]*2,
              ind_arg0_s+arg1_maps[n+offset_b]*2,
              ind_arg1_s+arg2_maps[n+offset_b]*4,
              ind_arg1_s+arg3_maps[n+offset_b]*4,
              ind_arg2_s+arg4_maps[n+offset_b]*1,
              ind_arg2_s+arg5_maps[n+offset_b]*1,
              arg6_l,
              arg7_l );

    // store local variables

    int arg6_map = arg6_maps[n+offset_b];
    int arg7_map = arg7_maps[n+offset_b];

    for (int d=0; d<4; d++)
      ind_arg3_s[d+arg6_map*4] += arg6_l[d];

    for (int d=0; d<4; d++)
      ind_arg3_s[d+arg7_map*4] += arg7_l[d];
  }

  // apply pointered write/increment

  for (int n=0; n<ind_arg3_size; n++)
    for (int d=0; d<4; d++)
      ind_arg3[d+ind_arg3_map[n]*4] += ind_arg3_s[d+n*4];

}


// host stub function

void op_par_loop_res_calc(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1,
  op_arg arg2,
  op_arg arg3,
  op_arg arg4,
  op_arg arg5,
  op_arg arg6,
  op_arg arg7 ){


  int    nargs   = 8;
  op_arg args[8] = {arg0,arg1,arg2,arg3,arg4,arg5,arg6,arg7};

  int    ninds   = 4;
  int    inds[8] = {0,0,1,1,2,2,3,3};

  if (OP_diags>2) {
    printf(" kernel routine with indirection: res_calc \n");
  }

  // get plan

  #ifdef OP_PART_SIZE_2
    int part_size = OP_PART_SIZE_2;
  #else
    int part_size = OP_part_size;
  #endif

  op_mpi_halo_exchanges(set, nargs, args);

  // initialise timers

  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timers(&cpu_t1, &wall_t1);

  // set number of threads

#ifdef _OPENMP
  int nthreads = omp_get_max_threads( );
#else
  int nthreads = 1;
#endif

  if (set->size >0) {


  op_plan *Plan = op_plan_get(name,set,part_size,nargs,args,ninds,inds);
  // execute plan

  int block_offset = 0;

  for (int col=0; col < Plan->ncolors; col++) {
    if (col==Plan->ncolors_core) op_mpi_wait_all(nargs, args);

    int nblocks = Plan->ncolblk[col];

#pragma omp parallel for
      for (int blockIdx=0; blockIdx<nblocks; blockIdx++)
      op_x86_res_calc( blockIdx,
         (double *)arg0.data, Plan->ind_maps[0],
         (double *)arg2.data, Plan->ind_maps[1],
         (double *)arg4.data, Plan->ind_maps[2],
         (double *)arg6.data, Plan->ind_maps[3],
         Plan->loc_maps[0],
         Plan->loc_maps[1],
         Plan->loc_maps[2],
         Plan->loc_maps[3],
         Plan->loc_maps[4],
         Plan->loc_maps[5],
         Plan->loc_maps[6],
         Plan->loc_maps[7],
         Plan->ind_sizes,
         Plan->ind_offs,
         block_offset,
         Plan->blkmap,
         Plan->offset,
         Plan->nelems,
         Plan->nthrcol,
         Plan->thrcol);

    block_offset += nblocks;
  }
  OP_kernels[2].transfer  += Plan->transfer;
  OP_kernels[2].transfer2 += Plan->transfer2;

  }


  // combine reduction data

  op_mpi_global_reduction(nargs, args);

  // update kernel record

  op_mpi_barrier();
  op_timers(&cpu_t2, &wall_t2);
  op_timing_realloc(2);
  OP_kernels[2].name      = name;
  OP_kernels[2].count    += 1;
  OP_kernels[2].time     += wall_t2 - wall_t1;
}

