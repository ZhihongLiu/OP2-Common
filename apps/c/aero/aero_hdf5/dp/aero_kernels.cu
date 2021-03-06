//
// auto-generated by op2.m on 29-Oct-2012 09:32:00
//

// header

#include "op_lib_cpp.h"

#include "op_cuda_rt_support.h"
#include "op_cuda_reduction.h"
// global constants

#ifndef MAX_CONST_SIZE
#define MAX_CONST_SIZE 128
#endif

__constant__ double gam;
__constant__ double gm1;
__constant__ double gm1i;
__constant__ double m2;
__constant__ double wtg1[2];
__constant__ double xi1[2];
__constant__ double Ng1[4];
__constant__ double Ng1_xi[4];
__constant__ double wtg2[4];
__constant__ double Ng2[16];
__constant__ double Ng2_xi[32];
__constant__ double minf;
__constant__ double freq;
__constant__ double kappa;
__constant__ double nmode;
__constant__ double mfan;
__constant__ int op2_stride;

#define OP2_STRIDE(arr, idx) arr[op2_stride*(idx)]

void op_decl_const_char(int dim, char const *type,
            int size, char *dat, char const *name) {
  if (!strcmp(name,"gam")) {
    cutilSafeCall(cudaMemcpyToSymbol(gam, dat, dim*size));
  } else  if (!strcmp(name,"gm1")) {
    cutilSafeCall(cudaMemcpyToSymbol(gm1, dat, dim*size));
  } else  if (!strcmp(name,"gm1i")) {
    cutilSafeCall(cudaMemcpyToSymbol(gm1i, dat, dim*size));
  } else  if (!strcmp(name,"m2")) {
    cutilSafeCall(cudaMemcpyToSymbol(m2, dat, dim*size));
  } else  if (!strcmp(name,"wtg1")) {
    cutilSafeCall(cudaMemcpyToSymbol(wtg1, dat, dim*size));
  } else  if (!strcmp(name,"xi1")) {
    cutilSafeCall(cudaMemcpyToSymbol(xi1, dat, dim*size));
  } else  if (!strcmp(name,"Ng1")) {
    cutilSafeCall(cudaMemcpyToSymbol(Ng1, dat, dim*size));
  } else  if (!strcmp(name,"Ng1_xi")) {
    cutilSafeCall(cudaMemcpyToSymbol(Ng1_xi, dat, dim*size));
  } else  if (!strcmp(name,"wtg2")) {
    cutilSafeCall(cudaMemcpyToSymbol(wtg2, dat, dim*size));
  } else  if (!strcmp(name,"Ng2")) {
    cutilSafeCall(cudaMemcpyToSymbol(Ng2, dat, dim*size));
  } else  if (!strcmp(name,"Ng2_xi")) {
    cutilSafeCall(cudaMemcpyToSymbol(Ng2_xi, dat, dim*size));
  } else  if (!strcmp(name,"minf")) {
    cutilSafeCall(cudaMemcpyToSymbol(minf, dat, dim*size));
  } else  if (!strcmp(name,"freq")) {
    cutilSafeCall(cudaMemcpyToSymbol(freq, dat, dim*size));
  } else  if (!strcmp(name,"kappa")) {
    cutilSafeCall(cudaMemcpyToSymbol(kappa, dat, dim*size));
  } else  if (!strcmp(name,"nmode")) {
    cutilSafeCall(cudaMemcpyToSymbol(nmode, dat, dim*size));
  } else  if (!strcmp(name,"mfan")) {
    cutilSafeCall(cudaMemcpyToSymbol(mfan, dat, dim*size));
  } else {
    printf("error: unknown const name\n"); exit(1);
  }
}

// user kernel files

#include "res_calc_kernel.cu"
#include "dirichlet_kernel.cu"
#include "init_cg_kernel.cu"
#include "spMV_kernel.cu"
#include "dotPV_kernel.cu"
#include "updateUR_kernel.cu"
#include "dotR_kernel.cu"
#include "updateP_kernel.cu"
#include "update_kernel.cu"
