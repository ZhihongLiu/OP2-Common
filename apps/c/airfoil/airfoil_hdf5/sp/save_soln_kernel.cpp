//
// auto-generated by op2.py on 2013-10-29 14:09
//

//user function
#ifndef BEG
#define BEG 1
#endif
#include "save_soln.h"
inline void save_soln_vec(floatv *q, floatv *qold){
  for (int n=0; n<4; n++) qold[n] = q[n];
}


// host stub function
void op_par_loop_save_soln(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1){

  int nargs = 2;
  op_arg args[2];

  args[0] = arg0;
  args[1] = arg1;
  // set number of threads
  #ifdef _OPENMP
    int nthreads = omp_get_max_threads();
  #else
    int nthreads = 1;
  #endif

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(0);
  op_timers_core(&cpu_t1, &wall_t1);


  int exec_size = op_mpi_halo_exchanges(set, nargs, args);
  int set_size = ((set->size+set->exec_size-1)/16+1)*16; //align to 512 bits

  if (OP_diags>2) {
    printf(" kernel routine w/o indirection:  save_soln");
  }

  if (exec_size >0) {

    int chunk_size = (((set->size-1)/nthreads+1-1)/16+1)*16;
    #pragma omp parallel for
    for ( int thr=0; thr<nthreads; thr++ ){
      int start  = min(thr*chunk_size,set->size);
      int finish = min((thr+1)*chunk_size,set->size);
      for ( int n=start/16; n<finish/16; n++ ){
        floatv arg0_p[4] = {
          floatv(&((float*)arg0.data)[16*4 * n +0], 4),
          floatv(&((float*)arg0.data)[16*4 * n +1], 4),
          floatv(&((float*)arg0.data)[16*4 * n +2], 4),
          floatv(&((float*)arg0.data)[16*4 * n +3], 4)};
        floatv arg1_p[4];
        save_soln_vec(
          arg0_p,
          arg1_p);
        store_stride(arg1_p[0],&((float*)arg1.data)[16*4 * n +0], 4);
        store_stride(arg1_p[1],&((float*)arg1.data)[16*4 * n +1], 4);
        store_stride(arg1_p[2],&((float*)arg1.data)[16*4 * n +2], 4);
        store_stride(arg1_p[3],&((float*)arg1.data)[16*4 * n +3], 4);
      }
      for ( int n=(finish/16)*16; n<finish; n++ ){
        save_soln(
          &((float*)arg0.data)[4*n],
          &((float*)arg1.data)[4*n]);
      }
    }
  }

  // combine reduction data
  op_mpi_set_dirtybit(nargs, args);

  // update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[0].name      = name;
  OP_kernels[0].count    += 1;
  OP_kernels[0].time     += wall_t2 - wall_t1;
  OP_kernels[0].transfer += (float)set->size * arg0.size;
  OP_kernels[0].transfer += (float)set->size * arg1.size * 2.0f;
}
