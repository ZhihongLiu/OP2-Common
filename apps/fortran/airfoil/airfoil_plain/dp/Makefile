PREPROCESSOR = -fpp

CPPLINK = -lstdc++
OP2 = $(OP2_INSTALL_PATH)

F_OP2_MOD = $(OP2)/fortran/mod/$(OP2_COMPILER)
F_OP2_LIB = $(OP2)/fortran/lib

HDF5_LIB = -L$(HDF5_INSTALL_PATH)/lib -lhdf5
MPI_LIB = -L$(MPI_INSTALL_PATH)/lib -lmpi
PARMETIS_INC 	= -I$(PARMETIS_INSTALL_PATH) -DHAVE_PARMETIS
PARMETIS_LIB 	= -L$(PARMETIS_INSTALL_PATH) -lparmetis \
            -L$(PARMETIS_INSTALL_PATH) -lmetis

PTSCOTCH_INC 	= -I$(PTSCOTCH_INSTALL_PATH)/include -DHAVE_PTSCOTCH
PTSCOTCH_LIB 	= -L$(PTSCOTCH_INSTALL_PATH)/lib/ -lptscotch \
                  -L$(PTSCOTCH_INSTALL_PATH)/lib/ -lptscotcherr


ifeq ($(OP2_COMPILER),gnu)

else
ifeq ($(OP2_COMPILER),pgi)
IC = pgcc
FC = pgfortran -Mcuda=cuda4.2
OPT = -DOP_PART_SIZE_1=$(PART_SIZE_ENV) -O2
OPENMP = -mp
CPPLINK = -pgcpplibs
else
ifeq ($(OP2_COMPILER),intel)
IC = icc
FC = ifort
OPENMP = -openmp
OPT = -DOP_PART_SIZE_1=$(PART_SIZE_ENV) -O2 -xSSE4.2 -vec-report
CPPLINK = -lstdc++
else
print:
	echo "unrecognised value for OP2_COMPILER"
endif
endif
endif

FLINK = -L$(F_OP2_LIB)
FMODS = -I$(F_OP2_MOD)

all: clean airfoil_seq airfoil_openmp_$(PART_SIZE_ENV) airfoil_cuda

airfoil_seq: airfoil_seqfun.F90 airfoil.F90 constants.F90 input.F90
	$(FC) $(OPT) $(FMODS) -c constants.F90 airfoil_seqfun.F90 input.F90 airfoil.F90
	$(FC) $(OPT) $(FLINK) constants.o airfoil.o input.o airfoil_seqfun.o -o airfoil_seq -lop2_for_seq

airfoil_openmp_$(PART_SIZE_ENV): airfoil_op.F90 save_soln_kernel.F90 adt_calc_kernel.F90 \
	res_calc_kernel.F90 bres_calc_kernel.F90 update_kernel.F90 constants.F90 input.F90 input.F90
	$(FC) $(OPT) $(OPENMP) $(FMODS) -c constants.F90 input.F90 \
	save_soln_kernel.F90 update_kernel.F90 bres_calc_kernel.F90 \
	res_calc_kernel.F90 adt_calc_kernel.F90 airfoil_op.F90 input.F90
	$(FC) $(OPT) $(OPENMP) $(FLINK) constants.o input.o input.o \
	save_soln_kernel.o adt_calc_kernel.o res_calc_kernel.o \
	bres_calc_kernel.o update_kernel.o airfoil_op.o -o airfoil_openmp_$(PART_SIZE_ENV) -lop2_for_openmp

airfoil_cuda: airfoil_op.F90 save_soln_kernel.CUF adt_calc_kernel.CUF input.F90 \
	res_calc_kernel.CUF bres_calc_kernel.CUF update_kernel.CUF constants.F90 input.F90 cudaConfigurationParams.F95
	$(FC) $(OPT) $(OPENMP) $(FMODS) $(FLINK) -DOP2_WITH_CUDAFOR cudaConfigurationParams.F95 constants.F90 input.F90 \
	save_soln_kernel.CUF update_kernel.CUF bres_calc_kernel.CUF \
	res_calc_kernel.CUF adt_calc_kernel.CUF airfoil_op.F90 -o airfoil_cuda -lop2_for_cuda

clean:
	rm -f *.o
	rm -f *.mod
	rm -f $(EXEC)
	rm -f *~
	rm -f airfoil_seq
	rm -f airfoil_openmp_$(PART_SIZE_ENV)
	rm -f airfoil_cuda