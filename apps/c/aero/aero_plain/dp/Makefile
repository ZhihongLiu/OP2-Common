#
# The following environment variables should be predefined:
#
# CUDA_INSTALL_PATH
# PARMETIS_INSTALL_PATH
# PTSCOTCH_INSTALL_PATH
# HDF5_INSTALL_PATH
#
# OP2_INSTALL_PATH
# OP2_COMPILER (gnu,intel,etc)
#

#
# set paths for header files and libraries
#
OP2_INC		= -I$(OP2_INSTALL_PATH)/c/include
OP2_LIB		= -L$(OP2_INSTALL_PATH)/c/lib

CUDA_INC	= -I$(CUDA_INSTALL_PATH)/include
CUDA_LIB	= -L$(CUDA_INSTALL_PATH)/lib64


ifeq ($(OP2_COMPILER),gnu)
  CPP		= g++
  CPPFLAGS	= -g -fPIC -DUNIX -Wall
#  CPPFLAGS	= -O3 -fPIC -DUNIX -Wall -Wextra
  OMPFLAGS	= -fopenmp
  MPICPP	= /usr/bin/mpiCC
  MPIFLAGS	= $(CCFLAGS)
else
ifeq ($(OP2_COMPILER),intel)
  CPP		= icpc
  CPPFLAGS	= -O3 -vec-report -xAVX -DMPICH_IGNORE_CXX_SEEK #-g #-DCOMM_PERF #-DDEBUG
  OMPFLAGS	= -openmp -openmp-report2
  MPICPP	= $(MPI_INSTALL_PATH)/bin/mpiCC
  MPIFLAGS	= $(CPPFLAGS)
else
print:
	@echo "unrecognised value for OP2_COMPILER"
endif
endif

#
# set flags for NVCC compilation and linking
#
ifndef NV_ARCH
  MESSAGE=select an NVIDA device to compile in CUDA, e.g. make NV_ARCH=KEPLER
  NV_ARCH=Kepler
endif
ifeq ($(NV_ARCH),Fermi)
  CODE_GEN_CUDA=-gencode arch=compute_20,code=sm_21
else
ifeq ($(NV_ARCH),Kepler)
  CODE_GEN_CUDA=-gencode arch=compute_35,code=sm_35
endif
endif

NVCCFLAGS       = -O3 $(CODE_GEN_CUDA) -m64 -Xptxas -dlcm=ca -Xptxas=-v -use_fast_math #-g -G -O0

#
# partitioning software for MPI versions
#
PARMETIS_VER=4
ifeq ($(PARMETIS_VER),4)
  PARMETIS_INC = -I$(PARMETIS_INSTALL_PATH)/include -DHAVE_PARMETIS -DPARMETIS_VER_4
  PARMETIS_LIB = -L$(PARMETIS_INSTALL_PATH)/lib -lparmetis -lmetis
else
  PARMETIS_INC = -I$(PARMETIS_INSTALL_PATH)/ -DHAVE_PARMETIS
  PARMETIS_LIB = -L$(PARMETIS_INSTALL_PATH)/ -lparmetis -lmetis
endif

PTSCOTCH_INC 	= -I$(PTSCOTCH_INSTALL_PATH)/include -DHAVE_PTSCOTCH
PTSCOTCH_LIB 	= -L$(PTSCOTCH_INSTALL_PATH)/lib/ -lptscotch \
                  -L$(PTSCOTCH_INSTALL_PATH)/lib/ -lptscotcherr

HDF5_INC = -I$(HDF5_INSTALL_PATH)/include
HDF5_LIB = -L$(HDF5_INSTALL_PATH)/lib -lhdf5 -lz

#
# master to make all versions
#

all: clean aero_seq aero_openmp aero_cuda aero_mpi aero_mpi_openmp aero_mpi_cuda

#
# simple sequential version
#

aero_seq: aero.cpp Makefile
	     $(CPP) aero.cpp $(CPPFLAGS) $(OP2_INC) $(OP2_LIB) -lop2_seq -o aero_seq

#
# x86 version using kernel files generated by op2.m
#

aero_openmp:	aero_op.cpp aero_kernels.cpp \
                Makefile
		$(CPP) $(CPPFLAGS) $(OMPFLAGS) $(OP2_INC) $(OP2_LIB) \
		aero_op.cpp aero_kernels.cpp -lm -lop2_openmp -o aero_openmp

#
# CUDA version using kernel files generated by op2.m
#

aero_cuda:	aero_op.cpp aero_kernels_cu.o Makefile
		$(CPP) $(CPPFLAGS) $(CUDA_INC) $(OP2_INC) $(OP2_LIB) $(CUDA_LIB) \
		aero_op.cpp aero_kernels_cu.o -lcudart -lop2_cuda -o aero_cuda -DCUDA

aero_kernels_cu.o:	aero_kernels.cu      \
                Makefile
		nvcc  $(INC) $(NVCCFLAGS) $(OP2_INC) \
		-c -o aero_kernels_cu.o \
		aero_kernels.cu -DCUDA

#
# mpi with sequential-nodes version
#

aero_mpi: aero_mpi.cpp Makefile
	$(MPICPP) $(MPIFLAGS) aero_mpi.cpp $(OP2_INC) $(PARMETIS_INC) $(PTSCOTCH_INC) \
	$(OP2_LIB) -lop2_mpi $(PARMETIS_LIB) $(PTSCOTCH_LIB) -o aero_mpi

#
# mpi with openmp-nodes version
#

aero_mpi_openmp:	aero_mpi_op.cpp aero_kernels.cpp Makefile
		$(MPICPP) $(MPIFLAGS) $(OMPFLAGS) $(OP2_INC) $(PARMETIS_INC) $(PTSCOTCH_INC) \
		aero_mpi_op.cpp aero_kernels.cpp -lm $(OP2_LIB) -lop2_mpi $(PARMETIS_LIB) $(PTSCOTCH_LIB) \
		-o aero_mpi_openmp


#
# mpi with CUDA version
#

aero_mpi_cuda:	aero_mpi_op.cpp aero_kernels_mpi_cu.o Makefile
		$(MPICPP) $(MPIFLAGS) aero_mpi_op.cpp aero_kernels_mpi_cu.o  \
		$(CUDA_INC) $(OP2_INC) $(PARMETIS_INC) $(PTSCOTCH_INC) \
		$(OP2_LIB) -lop2_mpi_cuda $(PARMETIS_LIB) $(PTSCOTCH_LIB) \
		$(CUDA_LIB) -lcudart  -o aero_mpi_cuda

aero_kernels_mpi_cu.o:	aero_kernels.cu      \
                Makefile
		nvcc  $(INC) $(NVCCFLAGS) $(OP2_INC) -I $(MPI_INSTALL_PATH)/include \
		-c -o aero_kernels_mpi_cu.o \
		aero_kernels.cu -DCUDA


write_hdf5: write_hdf5.cpp
	$(MPICPP) $(MPIFLAGS) write_hdf5.cpp $(OP2_INC) $(PARMETIS_INC) $(PTSCOTCH_INC) $(HDF5_INC)\
	$(OP2_LIB) -lop2_mpi $(PARMETIS_LIB) $(PTSCOTCH_LIB) $(HDF5_LIB) -o write_hdf5


#
# cleanup
#

clean:
		rm -f aero_seq aero_openmp aero_cuda aero_mpi write_hdf5 aero_mpi_openmp aero_mpi_cuda *.o
