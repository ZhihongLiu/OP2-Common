
#ifndef __OP_SEQ_H
#define __OP_SEQ_H


#define MAX_SET_NUM 10
#define MAX_MAP_NUM 10
#define MAX_DAT_NUM 10
#define MAX_PLAN_NUM 100
#define MAX_KERN_NUM 100

#define OP_READ		1
#define OP_WRITE	2
#define OP_RW			3
#define OP_INC		4
#define OP_MIN		5
#define OP_MAX		6

typedef int op_access;


typedef struct {
  int         size,   // number of elements in set
							index;  // index into list of sets 
  char const *name;   // name of set
} op_set;

typedef struct {
  op_set      from,   // set map from
							to;     // set map to
  int         dim,    // dimension of map
							index,  // index into list of maps
							*map;   // array defining map
  char const *name;   // name of map
} op_map;


typedef struct {
  op_set      set;    // set on which data is defined
  int         dim,    // dimension of data
							index,  // index into list of datasets
							size;   // size of each element in dataset
  char       *dat,    // data on host
						 *dat_d;  // data on device (GPU)
  char const *type;   // datatype
	char const *name;   // name of dataset
} op_dat;


typedef struct {
  // input arguments
  char const  *name;
  int          set_index, nargs;
  int         *arg_idxs, *idxs, *map_idxs, *dims;
  char const **typs;
  int   *accs;
	
  // execution plan
  int        *nthrcol;  // number of thread colors for each block
  int        *thrcol;   // thread colors
  int        *offset;   // offset for primary set
  int       **ind_maps; // pointers for indirect datasets
  int        *ind_offs; // offsets for indirect datasets
  int        *ind_sizes;// sizes for indirect datasets
  short     **maps;     // regular pointers, renumbered as needed
  int        *nelems;   // number of elements in each block
  int         ncolors;  // number of block colors
  int        *ncolblk;  // number of blocks for each color
  int        *blkmap;   // block mapping
  int         nshared;  // bytes of shared memory required
  float       transfer; // bytes of data transfer per kernel call
  float       transfer2;// bytes of cache line per kernel call
} op_plan;

typedef struct {
  char const *name;     // name of kernel function
  int         count;    // number of times called
  float       time;     // total execution time
  float       transfer; // bytes of data transfer (used)
  float       transfer2;// bytes of data transfer (total)
} op_kernel;



#endif