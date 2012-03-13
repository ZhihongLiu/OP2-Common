//
// auto-generated by op2.m on 30-May-2011 22:03:11
//

#include <op_rt_support.h>


// user function

#include "adt_calc.h"


// x86 kernel function

void op_x86_adt_calc(
  int    blockIdx,
  double *ind_arg0, int *ind_arg0_maps,
  short *arg0_maps,
  short *arg1_maps,
  short *arg2_maps,
  short *arg3_maps,
  double *arg4,
  double *arg5,
  int   *ind_arg_sizes,
  int   *ind_arg_offs,
  int    block_offset,
  int   *blkmap,
  int   *offset,
  int   *nelems,
  int   *ncolors,
  int   *colors) {


  int   *ind_arg0_map, ind_arg0_size;
  double *ind_arg0_s;
  int    nelem, offset_b;

  char shared[64000];

  if (0==0) {

    // get sizes and shift pointers and direct-mapped data

    int blockId = blkmap[blockIdx + block_offset];
    nelem    = nelems[blockId];
    offset_b = offset[blockId];

    ind_arg0_size = ind_arg_sizes[0+blockId*1];

    ind_arg0_map = ind_arg0_maps + ind_arg_offs[0+blockId*1];

    // set shared memory pointers

    int nbytes = 0;
    ind_arg0_s = (double *) &shared[nbytes];
  }

  // copy indirect datasets into shared memory or zero increment

  for (int n=0; n<ind_arg0_size; n++)
    for (int d=0; d<2; d++)
      ind_arg0_s[d+n*2] = ind_arg0[d+ind_arg0_map[n]*2];

  // process set elements

  for (int n=0; n<nelem; n++) {

      // user-supplied kernel call

      adt_calc( ind_arg0_s+arg0_maps[n+offset_b]*2,
                ind_arg0_s+arg1_maps[n+offset_b]*2,
                ind_arg0_s+arg2_maps[n+offset_b]*2,
                ind_arg0_s+arg3_maps[n+offset_b]*2,
                arg4+(n+offset_b)*4,
                arg5+(n+offset_b)*1 );
  }

}


// host stub function

void op_par_loop_adt_calc(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1,
  op_arg arg2,
  op_arg arg3,
  op_arg arg4,
  op_arg arg5 ){

  int nargs   = 6;
  op_arg args[6] = {arg0,arg1,arg2,arg3,arg4,arg5};

  int    ninds   = 1;
  int    inds[6] = {0,0,0,0,-1,-1};

  op_mpi_halo_exchanges(set, nargs, args);

  if (OP_diags>2) {
    printf(" kernel routine with indirection: adt_calc \n");
  }

  op_plan *Plan;
  double cpu_t1, cpu_t2, wall_t1, wall_t2;

  // get plan

  #ifdef OP_PART_SIZE_1
    int part_size = OP_PART_SIZE_1;
  #else
    int part_size = OP_part_size;
  #endif
      Plan = op_plan_get_offset(name,set,0,part_size,nargs,args,ninds,inds);

    // initialise timers
      op_timers_core(&cpu_t1, &wall_t1);

      // set number of threads

      #ifdef _OPENMP
      int nthreads = omp_get_max_threads( );
      #else
      int nthreads = 1;
      #endif

      // execute plan

  int block_offset = 0;

  for (int col=0; col < Plan->ncolors; col++) {
  if (col == Plan->ncolors_core) {
    op_mpi_wait_all(nargs,args);
  }
    int nblocks = Plan->ncolblk[col];

#pragma omp parallel for
    for (int blockIdx=0; blockIdx<nblocks; blockIdx++)
     op_x86_adt_calc( blockIdx,
       (double *)arg0.data, Plan->ind_maps[0],
       Plan->loc_maps[0],
       Plan->loc_maps[1],
       Plan->loc_maps[2],
       Plan->loc_maps[3],
       (double *)arg4.data,
       (double *)arg5.data,
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
    op_timers_core(&cpu_t2, &wall_t2);
    OP_kernels[1].time     += wall_t2 - wall_t1;
    OP_kernels[1].transfer  += Plan->transfer;
    OP_kernels[1].transfer2 += Plan->transfer2;



  //performe any global operations
  // - NONE


  // update kernel record
  op_timing_realloc(1);
  OP_kernels[1].name      = name;
  OP_kernels[1].count    += 1;
}

