/*
 * Open source copyright declaration based on BSD open source template:
 * http://www.opensource.org/licenses/bsd-license.php
 *
 * This file is part of the OP2 distribution.
 *
 * Copyright (c) 2011, Mike Giles and others. Please see the AUTHORS file in
 * the main source directory for a full list of copyright holders.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of Mike Giles may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Mike Giles ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Mike Giles BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * op_mpi_core.c
 *
 * Implements the OP2 Distributed memory (MPI) halo creation, halo exchange and
 * support utility routines/functions
 *
 * written by: Gihan R. Mudalige, (Started 01-03-2011)
 */


//mpi header
#include <mpi.h>

#include <op_lib_core.h>
#include <op_lib_c.h>
#include <op_util.h>

#include <op_mpi_core.h>

//
//MPI Communicator for halo creation and exchange
//

MPI_Comm OP_MPI_WORLD;

//
//MPI Halo related global variables
//

halo_list *OP_export_exec_list;//EEH list
halo_list *OP_import_exec_list;//IEH list

halo_list *OP_import_nonexec_list;//INH list
halo_list *OP_export_nonexec_list;//ENH list

//
// Partial halo exchange lists
//

int *OP_map_partial_exchange;//flag for each map
halo_list *OP_import_nonexec_permap;
halo_list *OP_export_nonexec_permap;
int *set_import_buffer_size;
//
//global array to hold dirty_bits for op_dats
//


/*table holding MPI performance of each loop
  (accessed via a hash of loop name) */
op_mpi_kernel *op_mpi_kernel_tab = NULL;

//
//global variables to hold partition information on an MPI rank
//

int OP_part_index = 0;
part *OP_part_list;

//
//Save original partition ranges
//

int** orig_part_range = NULL;

// Timing
double t1,t2,c1,c2;

/*******************************************************************************
 * Routine to declare partition information for a given set
 *******************************************************************************/

void decl_partition(op_set set, int* g_index, int* partition)
{
  part p = (part) xmalloc(sizeof(part_core));
  p->set = set;
  p->g_index = g_index;
  p->elem_part = partition;
  p->is_partitioned = 0;
  OP_part_list[set->index] = p;
  OP_part_index++;
}

/*******************************************************************************
 * Routine to get partition range on all mpi ranks for all sets
 *******************************************************************************/

void get_part_range(int** part_range, int my_rank, int comm_size, MPI_Comm Comm)
{
  (void)my_rank;
  for(int s=0; s<OP_set_index; s++) {
    op_set set=OP_set_list[s];

    int* sizes = (int *)xmalloc(sizeof(int)*comm_size);
    MPI_Allgather(&set->size, 1, MPI_INT, sizes, 1, MPI_INT, Comm);

    part_range[set->index] = (int *)xmalloc(2*comm_size*sizeof(int));

    int disp = 0;
    for(int i = 0; i<comm_size; i++){
      part_range[set->index][2*i] = disp;
      disp = disp + sizes[i] - 1;
      part_range[set->index][2*i+1] = disp;
      disp++;
#ifdef DEBUG
      if(my_rank == MPI_ROOT && OP_diags > 5)
        printf("range of %10s in rank %d: %d-%d\n",set->name,i,
            part_range[set->index][2*i], part_range[set->index][2*i+1]);
#endif
    }
    free(sizes);
  }
}

/*******************************************************************************
 * Routine to get partition (i.e. mpi rank) where global_index is located and
 * its local index
 *******************************************************************************/

int get_partition(int global_index, int* part_range, int* local_index,
                  int comm_size)
{
  for(int i = 0; i<comm_size; i++)
  {
    if (global_index >= part_range[2*i] &&
        global_index <= part_range[2*i+1])
    {
      *local_index = global_index -  part_range[2*i];
      return i;
    }
  }
  printf("Error: orphan global index\n");
  MPI_Abort(MPI_COMM_WORLD, 2);
  return -1;
}

/*******************************************************************************
 * Routine to convert a local index in to a global index
 *******************************************************************************/

int get_global_index(int local_index, int partition, int* part_range,
                     int comm_size)
{
  (void)comm_size;
  int g_index = part_range[2*partition]+local_index;
#ifdef DEBUG
  if(g_index > part_range[2*(comm_size-1)+1] && OP_diags>2 )
    printf("Global index larger than set size\n");
#endif
  return g_index;
}

/*******************************************************************************
 * Routine to find the MPI neighbors given a halo list
 *******************************************************************************/

void find_neighbors_set(halo_list List, int* neighbors, int* sizes,
                        int* ranks_size, int my_rank, int comm_size, MPI_Comm Comm)
{
  int* temp = (int*)xmalloc(comm_size*sizeof(int));
  int* r_temp = (int*)xmalloc(comm_size*comm_size*sizeof(int));

  for(int r = 0;r<comm_size*comm_size;r++)r_temp[r] = -99;
  for(int r = 0;r<comm_size;r++)temp[r] = -99;

  int n = 0;

  for(int r =0; r<comm_size; r++)
  {
    if(List->ranks[r]>=0) temp[List->ranks[r]] = List->sizes[r];
  }

  MPI_Allgather( temp, comm_size, MPI_INT, r_temp,
      comm_size,MPI_INT,Comm);

  for(int i=0; i<comm_size; i++)
  {
    if(i != my_rank)
    {
      if( r_temp[i*comm_size+my_rank] > 0)
      {
        neighbors[n] = i;
        sizes[n] = r_temp[i*comm_size+my_rank];
        n++;
      }
    }
  }
  *ranks_size = n;
  free(temp);free(r_temp);
}

/*******************************************************************************
 * Routine to create a generic halo list
 * (used in both import and export list creation)
 *******************************************************************************/

void create_list(int* list, int* ranks, int* disps, int* sizes, int* ranks_size,
    int* total, int* temp_list, int size, int comm_size, int my_rank)
{
  (void)my_rank;
  int index = 0;
  int total_size = 0;
  if(size < 0)printf("problem\n");
  //negative values set as an initialisation
  for(int r = 0;r<comm_size;r++)
  {
    disps[r] = ranks[r] = -99;
    sizes[r] = 0;
  }
  for(int r = 0;r<comm_size;r++)
  {
    sizes[index] = disps[index] = 0;

    int* temp = (int *)xmalloc((size/2)*sizeof(int));
    for(int i = 0;i<size;i=i+2)
    {
      if(temp_list[i]==r)
        temp[sizes[index]++] = temp_list[i+1];
    }
    if(sizes[index]>0)
    {
      ranks[index] = r;
      //sort temp,
      quickSort(temp,0,sizes[index]-1);
      //eliminate duplicates in temp
      sizes[index] = removeDups(temp, sizes[index]);
      total_size = total_size + sizes[index];

      if(index > 0)
        disps[index] = disps[index-1] +  sizes[index-1];
      //add to end of exp_list
      for(int e = 0;e<sizes[index];e++)
        list[disps[index]+e] = temp[e];

      index++;
    }
    free(temp);
  }

  *total = total_size;
  *ranks_size = index;
}

/*******************************************************************************
 * Routine to create an export list
 *******************************************************************************/

void create_export_list(op_set set, int* temp_list, halo_list h_list, int size,
    int comm_size, int my_rank)
{
  int* ranks = (int *)xmalloc(comm_size*sizeof(int));
  int* list = (int *)xmalloc((size/2)*sizeof(int));
  int* disps = (int *)xmalloc(comm_size*sizeof(int));
  int* sizes = (int *)xmalloc(comm_size*sizeof(int));

  int ranks_size = 0;
  int total_size = 0;

  create_list(list, ranks, disps, sizes, &ranks_size, &total_size,
      temp_list, size, comm_size, my_rank);


  h_list->set = set;
  h_list->size = total_size;
  h_list->ranks = ranks;
  h_list->ranks_size = ranks_size;
  h_list->disps = disps;
  h_list->sizes = sizes;
  h_list->list = list;
}

/*******************************************************************************
 * Routine to create an import list
 *******************************************************************************/

void create_import_list(op_set set, int* temp_list, halo_list h_list,
                        int total_size, int* ranks, int* sizes, int ranks_size,
                        int comm_size, int my_rank)
{
  (void)my_rank;
  int* disps = (int *)xmalloc(comm_size*sizeof(int));
  disps[0] = 0;
  for(int i=0; i<ranks_size; i++)
  {
    if(i>0)disps[i] = disps[i-1]+sizes[i-1];
  }

  h_list->set = set;
  h_list->size = total_size;
  h_list->ranks = ranks;
  h_list->ranks_size = ranks_size;
  h_list->disps = disps;
  h_list->sizes = sizes;
  h_list->list = temp_list;
}

/*******************************************************************************
 * Routine to create an nonexec-import list (only a wrapper)
 *******************************************************************************/

static void create_nonexec_import_list(op_set set, int* temp_list, halo_list h_list,
                                       int size, int comm_size, int my_rank)
{
  create_export_list(set, temp_list, h_list, size, comm_size, my_rank);
}

/*******************************************************************************
 * Routine to create an nonexec-export list (only a wrapper)
 *******************************************************************************/

static void create_nonexec_export_list(op_set set, int* temp_list, halo_list h_list,
                                       int total_size, int* ranks, int* sizes,
                                       int ranks_size, int comm_size, int my_rank)
{
  create_import_list(set, temp_list, h_list, total_size, ranks, sizes,
      ranks_size, comm_size, my_rank);
}


/*******************************************************************************
 * Check if a given op_map is an on-to map from the from-set to the to-set
 * note: on large meshes this routine takes up a lot of memory due to memory
 * allocated for MPI_Allgathers, thus use only when debugging code
 *******************************************************************************/

int is_onto_map(op_map map)
{
  //create new communicator
  int my_rank, comm_size;
  MPI_Comm OP_CHECK_WORLD;
  MPI_Comm_dup(MPI_COMM_WORLD, &OP_CHECK_WORLD);
  MPI_Comm_rank(OP_CHECK_WORLD, &my_rank);
  MPI_Comm_size(OP_CHECK_WORLD, &comm_size);

  // Compute global partition range information for each set
  int** part_range = (int **)xmalloc(OP_set_index*sizeof(int*));
  get_part_range(part_range,my_rank,comm_size, OP_CHECK_WORLD);

  //mak a copy of the to-set elements of the map
  int* to_elem_copy = (int *)xmalloc(map->from->size*map->dim*sizeof(int));
  memcpy(to_elem_copy,(void *)map->map,map->from->size*map->dim*sizeof(int));

  //sort and remove duplicates from to_elem_copy
  quickSort(to_elem_copy, 0, map->from->size*map->dim - 1);
  int to_elem_copy_size = removeDups(to_elem_copy, map->from->size*map->dim);
  to_elem_copy = (int *)xrealloc(to_elem_copy,to_elem_copy_size*sizeof(int));

  //go through the to-set element range that this local MPI process holds
  //and collect the to-set elements not found in to_elem_copy
  int cap = 100; int count = 0;
  int* not_found = (int *)xmalloc(sizeof(int)*cap);
  for(int i = 0; i < map->to->size; i++)
  {
    int g_index = get_global_index(i, my_rank,
        part_range[map->to->index], comm_size);
    if(binary_search(to_elem_copy, i, 0, to_elem_copy_size-1) < 0)
    {
      //add to not_found list
      if(count >= cap)
      {
        cap = cap*2;
        not_found = (int *)xrealloc(not_found, cap*sizeof(int));
      }
      not_found[count++] = g_index;
    }
  }

  //
  //allreduce this not_found to form a global_not_found list
  //
  int recv_count[comm_size];
  MPI_Allgather(&count, 1, MPI_INT, recv_count, 1, MPI_INT, OP_CHECK_WORLD);

  //discover global size of the not_found_list
  int g_count = 0;
  for(int i = 0; i< comm_size; i++)g_count += recv_count[i];

  //prepare for an allgatherv
  int disp = 0;
  int* displs = (int *)xmalloc(comm_size*sizeof(int));
  for(int i = 0; i<comm_size; i++)
  {
    displs[i] =   disp;
    disp = disp + recv_count[i];
  }

  //allocate memory to hold the global_not_found list
  int *global_not_found = (int *)xmalloc(sizeof(int)*g_count);

  MPI_Allgatherv(not_found,count,MPI_INT, global_not_found,recv_count,displs,
      MPI_INT, OP_CHECK_WORLD);
  free(not_found);free(displs);

  //sort and remove duplicates of the global_not_found list
  if(g_count > 0)
  {
    quickSort(global_not_found, 0, g_count-1);
    g_count = removeDups(global_not_found, g_count);
    global_not_found = (int *)xrealloc(global_not_found, g_count*sizeof(int));
  }
  else
  {
    //nothing in the global_not_found list .. i.e. this is an on to map
    free(global_not_found);free(to_elem_copy);//free(displs);
    for(int i = 0; i<OP_set_index; i++)free(part_range[i]);free(part_range);
    return 1;
  }

  //see if any element in the global_not_found is found in the local map-copy
  //and add it to a "found" list
  cap = 100; count = 0;
  int* found = (int *)xmalloc(sizeof(int)*cap);
  for(int i = 0; i < g_count; i++)
  {
    if(binary_search(to_elem_copy, global_not_found[i], 0,
          to_elem_copy_size-1) >= 0)
    {
      //add to found list
      if(count >= cap)
      {
        cap = cap*2;
        found = (int *)xrealloc(found, cap*sizeof(int));
      }
      found[count++] = global_not_found[i];
    }
  }
  free(global_not_found);

  //
  //allreduce the "found" elements to form a global_found list
  //
  //recv_count[comm_size];
  MPI_Allgather(&count, 1, MPI_INT, recv_count, 1, MPI_INT, OP_CHECK_WORLD);

  //discover global size of the found_list
  int g_found_count = 0;
  for(int i = 0; i< comm_size; i++)g_found_count += recv_count[i];

  //prepare for an allgatherv
  disp = 0;
  displs = (int *)xmalloc(comm_size*sizeof(int));
  for(int i = 0; i<comm_size; i++)
  {
    displs[i] =   disp;
    disp = disp + recv_count[i];
  }

  //allocate memory to hold the global_found list
  int *global_found = (int *)xmalloc(sizeof(int)*g_found_count);

  MPI_Allgatherv(found,count,MPI_INT, global_found,recv_count,displs,
      MPI_INT, OP_CHECK_WORLD);
  free(found);

  //sort global_found list and remove duplicates
  if(g_count > 0)
  {
    quickSort(global_found, 0, g_found_count-1);
    g_found_count = removeDups(global_found, g_found_count);
    global_found = (int *)xrealloc(global_found, g_found_count*sizeof(int));
  }

  //if the global_found list size is smaller than the globla_not_found list size
  //then map is not an on_to map
  int result = 0;
  if(g_found_count == g_count)
    result = 1;

  free(global_found);free(displs);
  for(int i = 0; i<OP_set_index; i++)free(part_range[i]);free(part_range);
  MPI_Comm_free(&OP_CHECK_WORLD);
  free(to_elem_copy);

  return result;
}

/*******************************************************************************
 * Main MPI halo creation routine
 *******************************************************************************/

void op_halo_create()
{
  //declare timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  double time;
  double max_time;
  op_timers(&cpu_t1, &wall_t1); //timer start for list create

  //create new communicator for OP mpi operation
  int my_rank, comm_size;
  MPI_Comm_dup(MPI_COMM_WORLD, &OP_MPI_WORLD);
  MPI_Comm_rank(OP_MPI_WORLD, &my_rank);
  MPI_Comm_size(OP_MPI_WORLD, &comm_size);

  /* Compute global partition range information for each set*/
  int** part_range = (int **)xmalloc(OP_set_index*sizeof(int*));
  get_part_range(part_range,my_rank,comm_size, OP_MPI_WORLD);

  //save this partition range information if it is not already saved during
  //a call to some partitioning routine
  if(orig_part_range == NULL)
  {
    orig_part_range = (int **)xmalloc(OP_set_index*sizeof(int*));
    for(int s = 0; s< OP_set_index; s++)
    {
      op_set set=OP_set_list[s];
      orig_part_range[set->index] = (int *)xmalloc(2*comm_size*sizeof(int));
      for(int j = 0; j<comm_size; j++){
        orig_part_range[set->index][2*j] = part_range[set->index][2*j];
        orig_part_range[set->index][2*j+1] = part_range[set->index][2*j+1];
      }
    }
  }

  OP_export_exec_list = (halo_list *)xmalloc(OP_set_index*sizeof(halo_list));

  /*----- STEP 1 - Construct export lists for execute set elements and related
    mapping table entries -----*/

  //declare temporaty scratch variables to hold set export lists and mapping
  //table export lists
  int s_i;
  int* set_list;

  int cap_s = 1000; //keep track of the temp array capacities


  for(int s=0; s<OP_set_index; s++){ //for each set
    op_set set=OP_set_list[s];

    //create a temporaty scratch space to hold export list for this set
    s_i = 0;cap_s = 1000;
    set_list = (int *)xmalloc(cap_s*sizeof(int));

    for(int e=0; e<set->size;e++){//for each elment of this set
      for(int m=0; m<OP_map_index; m++) { //for each maping table
        op_map map=OP_map_list[m];

        if(compare_sets(map->from,set)==1) //need to select mappings
          //FROM this set
        {
          int part, local_index;
          for(int j=0; j<map->dim; j++) { //for each element
            //pointed at by this entry
            part = get_partition(map->map[e*map->dim+j],
                part_range[map->to->index],&local_index,comm_size);
            if(s_i>=cap_s)
            {
              cap_s = cap_s*2;
              set_list = (int *)xrealloc(set_list,cap_s*sizeof(int));
            }

            if(part != my_rank){
              set_list[s_i++] = part; //add to set export list
              set_list[s_i++] = e;
            }
          }
        }
      }
    }

    //create set export list
    //printf("creating set export list for set %10s of size %d\n",
    //set->name,s_i);
    halo_list h_list= (halo_list)xmalloc(sizeof(halo_list_core));
    create_export_list(set,set_list, h_list, s_i, comm_size, my_rank);
    OP_export_exec_list[set->index] = h_list;
    free(set_list);//free temp list
  }

  /*---- STEP 2 - construct import lists for mappings and execute sets------*/

  OP_import_exec_list = (halo_list *)xmalloc(OP_set_index*sizeof(halo_list));

  int *neighbors, *sizes;
  int ranks_size;

  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];

    //-----Discover neighbors-----
    ranks_size = 0;
    neighbors = (int *)xmalloc(comm_size*sizeof(int));
    sizes = (int *)xmalloc(comm_size*sizeof(int));

    halo_list list = OP_export_exec_list[set->index];

    find_neighbors_set(list,neighbors,sizes,&ranks_size,my_rank,
        comm_size, OP_MPI_WORLD);
    MPI_Request request_send[list->ranks_size];

    int* rbuf, cap = 0, index = 0;

    for(int i=0; i<list->ranks_size; i++) {
      //printf("export from %d to %d set %10s, list of size %d \n",
      //my_rank,list->ranks[i],set->name,list->sizes[i]);
      int* sbuf = &list->list[list->disps[i]];
      MPI_Isend( sbuf,  list->sizes[i],  MPI_INT, list->ranks[i], s,
          OP_MPI_WORLD, &request_send[i] );
    }

    for(int i=0; i< ranks_size; i++) cap = cap + sizes[i];
    int* temp = (int *)xmalloc(cap*sizeof(int));

    //import this list from those neighbors
    for(int i=0; i<ranks_size; i++) {
      //printf("import from %d to %d set %10s, list of size %d\n",
      //neighbors[i], my_rank, set->name, sizes[i]);
      rbuf = (int *)xmalloc(sizes[i]*sizeof(int));
      MPI_Recv(rbuf, sizes[i], MPI_INT, neighbors[i],s, OP_MPI_WORLD,
          MPI_STATUSES_IGNORE );
      memcpy(&temp[index],(void *)&rbuf[0],sizes[i]*sizeof(int));
      index = index + sizes[i];
      free(rbuf);
    }

    MPI_Waitall(list->ranks_size,request_send, MPI_STATUSES_IGNORE );

    //create import lists
    //printf("creating importlist with number of neighbors %d\n",ranks_size);
    halo_list h_list= (halo_list)xmalloc(sizeof(halo_list_core));
    create_import_list(set, temp, h_list, index,neighbors, sizes,
        ranks_size, comm_size, my_rank);
    OP_import_exec_list[set->index] = h_list;
  }

  /*--STEP 3 -Exchange mapping table entries using the import/export lists--*/

  for(int m=0; m<OP_map_index; m++) { //for each maping table
    op_map map=OP_map_list[m];
    halo_list i_list = OP_import_exec_list[map->from->index];
    halo_list e_list = OP_export_exec_list[map->from->index];

    MPI_Request request_send[e_list->ranks_size];

    //prepare bits of the mapping tables to be exported
    int** sbuf = (int **)xmalloc(e_list->ranks_size*sizeof(int *));

    for(int i=0; i < e_list->ranks_size; i++) {
      sbuf[i] = (int *)xmalloc(e_list->sizes[i]*map->dim*sizeof(int));
      for(int j = 0; j < e_list->sizes[i]; j++)
      {
        for(int p = 0; p < map->dim; p++)
        {
          sbuf[i][j*map->dim+p] =
            map->map[map->dim*(e_list->list[e_list->disps[i]+j])+p];
        }
      }
      //printf("\n export from %d to %d map %10s, number of elements of size %d | sending:\n ",
      //    my_rank,e_list.ranks[i],map.name,e_list.sizes[i]);
      MPI_Isend(sbuf[i],  map->dim*e_list->sizes[i],  MPI_INT,
          e_list->ranks[i], m, OP_MPI_WORLD, &request_send[i]);
    }

    //prepare space for the incomming mapping tables - realloc each
    //mapping tables in each mpi process
    OP_map_list[map->index]->map = (int *)xrealloc(OP_map_list[map->index]->map,
        (map->dim*(map->from->size+i_list->size))*sizeof(int));

    int init = map->dim*(map->from->size);
    for(int i=0; i<i_list->ranks_size; i++) {
      //printf("\n imported on to %d map %10s, number of elements of size %d | recieving: ",
      //    my_rank, map->name, i_list->size);
      MPI_Recv(&(OP_map_list[map->index]->
            map[init+i_list->disps[i]*map->dim]),
          map->dim*i_list->sizes[i], MPI_INT, i_list->ranks[i], m,
          OP_MPI_WORLD, MPI_STATUSES_IGNORE);
    }

    MPI_Waitall(e_list->ranks_size,request_send, MPI_STATUSES_IGNORE );
    for(int i=0; i < e_list->ranks_size; i++) free(sbuf[i]); free(sbuf);
  }

  /*-- STEP 4 - Create import lists for non-execute set elements using mapping
    table entries including the additional mapping table entries --*/

  OP_import_nonexec_list = (halo_list *)xmalloc(OP_set_index*sizeof(halo_list));
  OP_export_nonexec_list = (halo_list *)xmalloc(OP_set_index*sizeof(halo_list));

  //declare temporaty scratch variables to hold non-exec set export lists
  s_i = 0;
  set_list = NULL;
  cap_s = 1000; //keep track of the temp array capacity

  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];
    halo_list exec_set_list=OP_import_exec_list[set->index];

    //create a temporaty scratch space to hold nonexec export list for this set
    s_i = 0;
    set_list = (int *)xmalloc(cap_s*sizeof(int));

    for(int m=0; m<OP_map_index; m++) { //for each maping table
      op_map map=OP_map_list[m];
      halo_list exec_map_list=OP_import_exec_list[map->from->index];

      if(compare_sets(map->to,set)==1) //need to select mappings TO this set
      {
        //for each entry in this mapping table: original+execlist
        int len = map->from->size+exec_map_list->size;
        for(int e = 0; e<len; e++)
        {
          int part;
          int local_index;
          for(int j=0; j < map->dim; j++) { //for each element pointed
            //at by this entry
            part = get_partition(map->map[e*map->dim+j],
                part_range[map->to->index],&local_index,comm_size);

            if(s_i>=cap_s)
            {
              cap_s = cap_s*2;
              set_list = (int *)xrealloc(set_list,cap_s*sizeof(int));
            }

            if(part != my_rank)
            {
              int found = -1;
              //check in exec list
              int rank = binary_search(exec_set_list->ranks,
                  part, 0, exec_set_list->ranks_size-1);

              if(rank >= 0)
              {
                found = binary_search(exec_set_list->list,
                    local_index, exec_set_list->disps[rank],
                    exec_set_list->disps[rank]+
                    exec_set_list->sizes[rank]-1);
              }

              if(found < 0){
                // not in this partition and not found in
                //exec list
                //add to non-execute set_list
                set_list[s_i++] = part;
                set_list[s_i++] = local_index;
              }
            }
          }
        }
      }
    }

    //create non-exec set import list
    //printf("creating non-exec import list of size %d\n",s_i);
    halo_list h_list= (halo_list)xmalloc(sizeof(halo_list_core));
    create_nonexec_import_list(set,set_list, h_list, s_i, comm_size, my_rank);
    free(set_list);//free temp list
    OP_import_nonexec_list[set->index] = h_list;
  }


  /*----------- STEP 5 - construct non-execute set export lists -------------*/

  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];

    //-----Discover neighbors-----
    ranks_size = 0;
    neighbors = (int* )xmalloc(comm_size*sizeof(int));
    sizes = (int* )xmalloc(comm_size*sizeof(int));

    halo_list list=OP_import_nonexec_list[set->index];
    find_neighbors_set(list,neighbors,sizes,&ranks_size,my_rank,
        comm_size, OP_MPI_WORLD);

    MPI_Request request_send[list->ranks_size];
    int* rbuf, cap = 0, index = 0;

    for(int i=0; i<list->ranks_size; i++) {
      //printf("import to %d from %d set %10s, nonexec list of size %d | sending:\n",
      //    my_rank,list->ranks[i],set->name,list->sizes[i]);
      int* sbuf = &list->list[list->disps[i]];
      MPI_Isend( sbuf,  list->sizes[i],  MPI_INT, list->ranks[i], s,
          OP_MPI_WORLD, &request_send[i] );
    }

    for(int i=0; i< ranks_size; i++) cap = cap + sizes[i];
    int* temp = (int* )xmalloc(cap*sizeof(int));

    //export this list to those neighbors
    for(int i=0; i<ranks_size; i++) {
      //printf("export to %d from %d set %10s, list of size %d | recieving:\n",
      //    neighbors[i], my_rank, set->name, sizes[i]);
      rbuf = (int* )xmalloc(sizes[i]*sizeof(int));
      MPI_Recv(rbuf, sizes[i], MPI_INT, neighbors[i],s, OP_MPI_WORLD,
          MPI_STATUSES_IGNORE );
      memcpy(&temp[index],(void *)&rbuf[0],sizes[i]*sizeof(int));
      index = index + sizes[i];
      free(rbuf);
    }

    MPI_Waitall(list->ranks_size,request_send, MPI_STATUSES_IGNORE );

    //create import lists
    //printf("creating nonexec set export list with number of neighbors %d\n",ranks_size);
    halo_list h_list= (halo_list)xmalloc(sizeof(halo_list_core));
    create_nonexec_export_list(set, temp, h_list, index, neighbors, sizes,
        ranks_size, comm_size, my_rank);
    OP_export_nonexec_list[set->index] = h_list;
  }


  /*-STEP 6 - Exchange execute set elements/data using the import/export lists--*/

  for(int s=0; s<OP_set_index; s++){ //for each set
    op_set set=OP_set_list[s];
    halo_list i_list = OP_import_exec_list[set->index];
    halo_list e_list = OP_export_exec_list[set->index];

    //for each data array
    op_dat_entry *item; int d = -1; //d is just simply the tag for mpi comms
    TAILQ_FOREACH(item, &OP_dat_list, entries) {
      d++; //increase tag to do mpi comm for the next op_dat
      op_dat dat = item->dat;

      if(compare_sets(set,dat->set)==1)//if this data array is defined on this set
      {
        //printf("on rank %d, The data array is %10s\n",my_rank,dat->name);
        MPI_Request request_send[e_list->ranks_size];

        //prepare execute set element data to be exported
        char** sbuf = (char** )xmalloc(e_list->ranks_size*sizeof(char *));

        for(int i=0; i < e_list->ranks_size; i++) {
          sbuf[i] = (char *)xmalloc(e_list->sizes[i]*dat->size);
          for(int j = 0; j < e_list->sizes[i]; j++)
          {
            int set_elem_index = e_list->list[e_list->disps[i]+j];
            memcpy(&sbuf[i][j*dat->size],
                (void *)&dat->data[dat->size*(set_elem_index)],
                dat->size);
          }
          //printf("export from %d to %d data %10s, number of elements of size %d | sending:\n ",
          //    my_rank,e_list->ranks[i],dat->name,e_list->sizes[i]);
          MPI_Isend(sbuf[i],  dat->size*e_list->sizes[i],
              MPI_CHAR, e_list->ranks[i],
              d, OP_MPI_WORLD, &request_send[i]);
        }

        //prepare space for the incomming data - realloc each
        //data array in each mpi process
        dat->data = (char *)xrealloc(dat->data,(set->size+i_list->size)*dat->size);

        int init = set->size*dat->size;
        for(int i=0; i<i_list->ranks_size; i++) {
          MPI_Recv(&(dat->data[init+i_list->disps[i]*dat->size]),
              dat->size*i_list->sizes[i],
              MPI_CHAR, i_list->ranks[i], d,
              OP_MPI_WORLD, MPI_STATUSES_IGNORE);
        }

        MPI_Waitall(e_list->ranks_size,request_send,
            MPI_STATUSES_IGNORE );
        for(int i=0; i<e_list->ranks_size; i++) free(sbuf[i]);
        free(sbuf);
        //printf("imported on to %d data %10s, number of elements of size %d | recieving:\n ",
        //    my_rank, dat->name, i_list->size);
      }

    }
  }

  /*-STEP 7 - Exchange non-execute set elements/data using the import/export lists--*/

  for(int s=0; s<OP_set_index; s++){ //for each set
    op_set set=OP_set_list[s];
    halo_list i_list = OP_import_nonexec_list[set->index];
    halo_list e_list = OP_export_nonexec_list[set->index];

    //for each data array
    op_dat_entry *item; int d = -1; //d is just simply the tag for mpi comms
    TAILQ_FOREACH(item, &OP_dat_list, entries) {
      d++; //increase tag to do mpi comm for the next op_dat
      op_dat dat = item->dat;

      if(compare_sets(set,dat->set)==1)//if this data array is
        //defined on this set
      {
        //printf("on rank %d, The data array is %10s\n",my_rank,dat->name);
        MPI_Request request_send[e_list->ranks_size];

        //prepare non-execute set element data to be exported
        char** sbuf = (char** )xmalloc(e_list->ranks_size*sizeof(char *));

        for(int i=0; i < e_list->ranks_size; i++) {
          sbuf[i] = (char *)xmalloc(e_list->sizes[i]*dat->size);
          for(int j = 0; j < e_list->sizes[i]; j++)
          {
            int set_elem_index = e_list->list[e_list->disps[i]+j];
            memcpy(&sbuf[i][j*dat->size],
                (void *)&dat->data[dat->size*(set_elem_index)],dat->size);
          }
          MPI_Isend(sbuf[i],  dat->size*e_list->sizes[i],
              MPI_CHAR, e_list->ranks[i],
              d, OP_MPI_WORLD, &request_send[i]);
        }

        //prepare space for the incomming nonexec-data - realloc each
        //data array in each mpi process
        halo_list exec_i_list = OP_import_exec_list[set->index];

        dat->data = (char *)xrealloc(dat->data,
          (set->size+exec_i_list->size+i_list->size)*dat->size);

        int init = (set->size+exec_i_list->size)*dat->size;
        for(int i=0; i < i_list->ranks_size; i++) {
          MPI_Recv(&(dat->data[init+i_list->disps[i]*dat->size]),
              dat->size*i_list->sizes[i],
              MPI_CHAR, i_list->ranks[i], d,
              OP_MPI_WORLD, MPI_STATUSES_IGNORE);
        }

        MPI_Waitall(e_list->ranks_size,request_send, MPI_STATUSES_IGNORE );
        for(int i=0; i < e_list->ranks_size; i++) free(sbuf[i]); free(sbuf);
      }
    }
  }





  /*-STEP 8 ----------------- Renumber Mapping tables-----------------------*/

  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];

    for(int m=0; m<OP_map_index; m++) { //for each maping table
      op_map map=OP_map_list[m];

      if(compare_sets(map->to,set)==1) //need to select mappings TO this set
      {
        halo_list exec_set_list=OP_import_exec_list[set->index];
        halo_list nonexec_set_list=OP_import_nonexec_list[set->index];

        halo_list exec_map_list=OP_import_exec_list[map->from->index];

        //for each entry in this mapping table: original+execlist
        int len = map->from->size+exec_map_list->size;
        for(int e = 0; e < len; e++)
        {
          for(int j=0; j < map->dim; j++) { //for each element
            //pointed at by this entry
            int part;
            int local_index = 0;
            part = get_partition(map->map[e*map->dim+j],
                part_range[map->to->index],&local_index,comm_size);

            if(part == my_rank)
            {
              OP_map_list[map->index]->
                map[e*map->dim+j] = local_index;
            }
            else
            {
              int found = -1;
              //check in exec list
              int rank1 = binary_search(exec_set_list->ranks,
                  part, 0, exec_set_list->ranks_size-1);
              //check in nonexec list
              int rank2 = binary_search(nonexec_set_list->ranks,
                  part, 0, nonexec_set_list->ranks_size-1);

              if(rank1 >=0)
              {
                found = binary_search(exec_set_list->list,
                    local_index, exec_set_list->disps[rank1],
                    exec_set_list->disps[rank1]+
                    exec_set_list->sizes[rank1]-1);
                if(found>=0)
                {
                  OP_map_list[map->index]->map[e*map->dim+j] =
                    found + map->to->size ;
                }
              }

              if(rank2 >=0 && found <0)
              {
                found = binary_search(nonexec_set_list->list,
                    local_index, nonexec_set_list->disps[rank2],
                    nonexec_set_list->disps[rank2]+
                    nonexec_set_list->sizes[rank2]-1);
                if(found>=0)
                {
                  OP_map_list[map->index]->map[e*map->dim+j] =
                    found + set->size + exec_set_list->size;
                }
              }

              if(found < 0)
                printf("ERROR: Set %10s Element %d needed on rank %d \
                    from partition %d\n",
                    set->name, local_index, my_rank, part );
            }
          }
        }
      }
    }
  }



  /*-STEP 9 ---------------- Create MPI send Buffers-----------------------*/

  op_dat_entry *item;
  TAILQ_FOREACH(item, &OP_dat_list, entries) {
    op_dat dat = item->dat;

    op_mpi_buffer mpi_buf= (op_mpi_buffer)xmalloc(sizeof(op_mpi_buffer_core));

    halo_list exec_e_list = OP_export_exec_list[dat->set->index];
    halo_list nonexec_e_list = OP_export_nonexec_list[dat->set->index];

    mpi_buf->buf_exec = (char *)xmalloc((exec_e_list->size)*dat->size);
    mpi_buf->buf_nonexec = (char *)xmalloc((nonexec_e_list->size)*dat->size);

    halo_list exec_i_list = OP_import_exec_list[dat->set->index];
    halo_list nonexec_i_list = OP_import_nonexec_list[dat->set->index];

    mpi_buf->s_req = (MPI_Request *)xmalloc(sizeof(MPI_Request)*
        (exec_e_list->ranks_size + nonexec_e_list->ranks_size));
    mpi_buf->r_req = (MPI_Request *)xmalloc(sizeof(MPI_Request)*
        (exec_i_list->ranks_size + nonexec_i_list->ranks_size));

    mpi_buf->s_num_req = 0;
    mpi_buf->r_num_req = 0;
    dat->mpi_buffer = mpi_buf;
  }


  //set dirty bits of all data arrays to 0
  //for each data array
  item = NULL;
  TAILQ_FOREACH(item, &OP_dat_list, entries) {
    op_dat dat = item->dat;
    dat->dirtybit= 0;
  }


  /*-STEP 10 -------------------- Separate core elements------------------------*/

  int** core_elems = (int **)xmalloc(OP_set_index*sizeof(int *));
  int** exp_elems = (int **)xmalloc(OP_set_index*sizeof(int *));

  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];

    halo_list exec = OP_export_exec_list[set->index];
    halo_list nonexec = OP_export_nonexec_list[set->index];

    if(exec->size > 0)
    {
      exp_elems[set->index]= (int *)xmalloc(exec->size*sizeof(int));
      memcpy(exp_elems[set->index], exec->list, exec->size*sizeof(int));
      quickSort(exp_elems[set->index], 0, exec->size-1);

      int num_exp = removeDups(exp_elems[set->index], exec->size);
      core_elems[set->index] = (int *)xmalloc(set->size*sizeof(int ));
      int count = 0;
      for(int e=0; e < set->size;e++){//for each elment of this set

        if((binary_search(exp_elems[set->index], e, 0, num_exp-1) < 0))
        {
          core_elems[set->index][count++] = e;
        }
      }
      quickSort(core_elems[set->index], 0, count-1);

      if(count+num_exp != set->size) printf("sizes not equal\n");
      set->core_size = count;

      //for each data array defined on this set seperate its elements
      op_dat_entry *item;
      TAILQ_FOREACH(item, &OP_dat_list, entries) {
      op_dat dat = item->dat;

        if(compare_sets(set,dat->set)==1)//if this data array is
          //defined on this set
        {
          char* new_dat = (char* )xmalloc(set->size*dat->size);
          for(int i = 0; i<count; i++)
          {
            memcpy(&new_dat[i*dat->size],
                &dat->data[core_elems[set->index][i]*dat->size],
                dat->size);
          }
          for(int i = 0; i< num_exp; i++)
          {
            memcpy(&new_dat[(count+i)*dat->size],
                &dat->data[exp_elems[set->index][i]*dat->size],
                dat->size);
          }
          memcpy(&dat->data[0],&new_dat[0], set->size*dat->size);
          free(new_dat);
        }
      }

      //for each mapping defined from this set seperate its elements
      for(int m=0; m<OP_map_index; m++) { //for each set
        op_map map=OP_map_list[m];

        if(compare_sets(map->from,set)==1)//if this mapping is
          //defined from this set
        {
          int* new_map = (int *)xmalloc(set->size*map->dim*sizeof(int));
          for(int i = 0; i<count; i++)
          {
            memcpy(&new_map[i*map->dim],
                &map->map[core_elems[set->index][i]*map->dim],
                map->dim*sizeof(int));
          }
          for(int i = 0; i<num_exp; i++)
          {
            memcpy(&new_map[(count+i)*map->dim],
                &map->map[exp_elems[set->index][i]*map->dim],
                map->dim*sizeof(int));
          }
          memcpy(&map->map[0],&new_map[0],
              set->size*map->dim*sizeof(int));
          free(new_map);
        }
      }

      for(int i  = 0; i< exec->size;i++)
      {
        int index = binary_search(exp_elems[set->index],
            exec->list[i], 0, num_exp-1);
        if(index < 0)
          printf("Problem in seperating core elements - exec list\n");
        else exec->list[i] = count + index;
      }

      for(int i  = 0; i< nonexec->size;i++)
      {
        int index = binary_search(core_elems[set->index],
            nonexec->list[i], 0, count-1);
        if (index < 0)
        {
          index = binary_search(exp_elems[set->index],
              nonexec->list[i], 0, num_exp-1);
          if(index < 0)
            printf("Problem in seperating core elements - nonexec list\n");
          else nonexec->list[i] = count + index;
        }
        else nonexec->list[i] = index;
      }
    }
    else
    {
      core_elems[set->index] = (int *)xmalloc(set->size*sizeof(int ));
      exp_elems[set->index] = (int *)xmalloc(0*sizeof(int ));
      for(int e=0; e < set->size;e++){//for each elment of this set
        core_elems[set->index][e] = e;
      }
      set->core_size = set->size;
    }
  }

  //now need to renumber mapping tables as the elements are seperated
  for(int m=0; m<OP_map_index; m++) { //for each set
    op_map map=OP_map_list[m];

    halo_list exec_map_list=OP_import_exec_list[map->from->index];
    //for each entry in this mapping table: original+execlist
    int len = map->from->size+exec_map_list->size;
    for(int e = 0; e < len; e++)
    {
      for(int j=0; j < map->dim; j++) { //for each element pointed
        //at by this entry
        if(map->map[e*map->dim+j] < map->to->size)
        {
          int index = binary_search(core_elems[map->to->index],
              map->map[e*map->dim+j],
              0, map->to->core_size-1);
          if(index < 0)
          {
            index = binary_search(exp_elems[map->to->index],
                map->map[e*map->dim+j],
                0, (map->to->size) - (map->to->core_size) -1);
            if(index < 0)
              printf("Problem in seperating core elements - \
                  renumbering map\n");
            else OP_map_list[map->index]->map[e*map->dim+j] =
              map->to->core_size + index;
          }
          else OP_map_list[map->index]->map[e*map->dim+j] = index;
        }
      }
    }
  }


  /*-STEP 11 ----------- Save the original set element indexes------------------*/

  //if OP_part_list is empty, (i.e. no previous partitioning done) then
  //create it and store the seperation of elements using core_elems
  //and exp_elems
  if(OP_part_index != OP_set_index)
  {
    //allocate memory for list
    OP_part_list = (part *)xmalloc(OP_set_index*sizeof(part));

    for(int s=0; s<OP_set_index; s++) { //for each set
      op_set set=OP_set_list[s];
      //printf("set %s size = %d\n", set.name, set.size);
      int *g_index = (int *)xmalloc(sizeof(int)*set->size);
      int *partition = (int *)xmalloc(sizeof(int)*set->size);
      for(int i = 0; i< set->size; i++)
      {
        g_index[i] = get_global_index(i,my_rank,
            part_range[set->index],comm_size);
        partition[i] = my_rank;
      }
      decl_partition(set, g_index, partition);

      //combine core_elems and exp_elems to one memory block
      int* temp = (int *)xmalloc(sizeof(int)*set->size);
      memcpy(&temp[0], core_elems[set->index],
          set->core_size*sizeof(int));
      memcpy(&temp[set->core_size], exp_elems[set->index],
          (set->size - set->core_size)*sizeof(int));

      //update OP_part_list[set->index]->g_index
      for(int i = 0; i<set->size; i++)
      {
        temp[i] = OP_part_list[set->index]->g_index[temp[i]];
      }
      free(OP_part_list[set->index]->g_index);
      OP_part_list[set->index]->g_index = temp;
    }
  }
  else //OP_part_list exists (i.e. a partitioning has been done)
    //update the seperation of elements
  {
    for(int s=0; s<OP_set_index; s++) { //for each set
      op_set set=OP_set_list[s];

      //combine core_elems and exp_elems to one memory block
      int* temp = (int *)xmalloc(sizeof(int)*set->size);
      memcpy(&temp[0], core_elems[set->index],
          set->core_size*sizeof(int));
      memcpy(&temp[set->core_size], exp_elems[set->index],
          (set->size - set->core_size)*sizeof(int));

      //update OP_part_list[set->index]->g_index
      for(int i = 0; i<set->size; i++)
      {
        temp[i] = OP_part_list[set->index]->g_index[temp[i]];
      }
      free(OP_part_list[set->index]->g_index);
      OP_part_list[set->index]->g_index = temp;
    }
  }

  /*for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];
    printf("Original Index for set %s\n", set->name);
    for(int i=0; i<set->size; i++ )
    printf(" %d",OP_part_list[set->index]->g_index[i]);
    }*/

  //set up exec and nonexec sizes
  for(int s=0; s<OP_set_index; s++) { //for each set
    op_set set=OP_set_list[s];
    set->exec_size = OP_import_exec_list[set->index]->size;
    set->nonexec_size = OP_import_nonexec_list[set->index]->size;
  }

  /*-STEP 12 ---------- Clean up and Compute rough halo size numbers------------*/

  for(int i = 0; i<OP_set_index; i++)
  { free(part_range[i]);
    free(core_elems[i]); free(exp_elems[i]);
  }
  free(part_range);
  free(exp_elems); free(core_elems);

  op_timers(&cpu_t2, &wall_t2);  //timer stop for list create
  //compute import/export lists creation time
  time = wall_t2-wall_t1;
  MPI_Reduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

  //compute avg/min/max set sizes and exec sizes accross the MPI universe
  int avg_size = 0, min_size = 0, max_size = 0;
  for(int s = 0; s< OP_set_index; s++){
    op_set set=OP_set_list[s];

    //number of set elements first
    MPI_Reduce(&set->size, &avg_size,1, MPI_INT, MPI_SUM,
        MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&set->size, &min_size,1, MPI_INT, MPI_MIN,
        MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&set->size, &max_size,1, MPI_INT, MPI_MAX,
        MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("Num of %8s (avg | min | max)\n",set->name);
      printf("total elems         %10d %10d %10d\n",avg_size/comm_size, min_size, max_size);
    }

    avg_size = 0;min_size = 0; max_size = 0;


    //number of OWNED elements second
    MPI_Reduce(&set->core_size,
        &avg_size,1, MPI_INT, MPI_SUM, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&set->core_size,
        &min_size,1, MPI_INT, MPI_MIN, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&set->core_size,
        &max_size,1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("core elems         %10d %10d %10d \n",avg_size/comm_size, min_size, max_size);
    }
    avg_size = 0;min_size = 0; max_size = 0;


    //number of exec halo elements third
    MPI_Reduce(&OP_import_exec_list[set->index]->size,
        &avg_size,1, MPI_INT, MPI_SUM, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_exec_list[set->index]->size,
        &min_size,1, MPI_INT, MPI_MIN, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_exec_list[set->index]->size,
        &max_size,1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("exec halo elems     %10d %10d %10d \n", avg_size/comm_size, min_size, max_size);
    }
    avg_size = 0;min_size = 0; max_size = 0;

    //number of non-exec halo elements fourth
    MPI_Reduce(&OP_import_nonexec_list[set->index]->size,
        &avg_size,1, MPI_INT, MPI_SUM, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_nonexec_list[set->index]->size,
        &min_size,1, MPI_INT, MPI_MIN, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_nonexec_list[set->index]->size,
        &max_size,1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("non-exec halo elems %10d %10d %10d \n", avg_size/comm_size, min_size, max_size);
    }
    avg_size = 0;min_size = 0; max_size = 0;
    if(my_rank == MPI_ROOT)
    {
      printf("-----------------------------------------------------\n");
    }
  }

  if(my_rank == MPI_ROOT)
  {
    printf("\n\n");
  }

  //compute avg/min/max number of MPI neighbors per process accross the MPI universe
  avg_size = 0, min_size = 0, max_size = 0;
  for(int s = 0; s< OP_set_index; s++){
    op_set set=OP_set_list[s];

    //number of exec halo neighbors first
    MPI_Reduce(&OP_import_exec_list[set->index]->ranks_size,
        &avg_size,1, MPI_INT, MPI_SUM, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_exec_list[set->index]->ranks_size,
        &min_size,1, MPI_INT, MPI_MIN, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_exec_list[set->index]->ranks_size,
        &max_size,1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("MPI neighbors for exchanging %8s (avg | min | max)\n",set->name);
      printf("exec halo elems     %4d %4d %4d\n",avg_size/comm_size, min_size, max_size);


    }
    avg_size = 0;min_size = 0; max_size = 0;

    //number of non-exec halo neighbors second
    MPI_Reduce(&OP_import_nonexec_list[set->index]->ranks_size,
        &avg_size,1, MPI_INT, MPI_SUM, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_nonexec_list[set->index]->ranks_size,
        &min_size,1, MPI_INT, MPI_MIN, MPI_ROOT, OP_MPI_WORLD);
    MPI_Reduce(&OP_import_nonexec_list[set->index]->ranks_size,
        &max_size,1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_WORLD);

    if(my_rank == MPI_ROOT)
    {
      printf("non-exec halo elems %4d %4d %4d\n",avg_size/comm_size, min_size, max_size);
    }
    avg_size = 0;min_size = 0; max_size = 0;
    if(my_rank == MPI_ROOT)
    {
      printf("-----------------------------------------------------\n");
    }
  }

  //compute average worst case halo size in Bytes
  int tot_halo_size = 0;
  for(int s = 0; s< OP_set_index; s++){
    op_set set=OP_set_list[s];

    op_dat_entry *item;
    TAILQ_FOREACH(item, &OP_dat_list, entries) {
      op_dat dat = item->dat;

      if(compare_sets(dat->set,set)==1)
      {
        halo_list exec_imp = OP_import_exec_list[set->index];
        halo_list nonexec_imp= OP_import_nonexec_list[set->index];
        tot_halo_size = tot_halo_size + exec_imp->size*dat->size +
          nonexec_imp->size*dat->size;
      }
    }
  }
  int avg_halo_size;
  MPI_Reduce(&tot_halo_size, &avg_halo_size,1, MPI_INT, MPI_SUM,
      MPI_ROOT, OP_MPI_WORLD);

  //print performance results
  if(my_rank == MPI_ROOT)
  {
    printf("Max total halo creation time = %lf\n",max_time);
    printf("Average (worst case) Halo size = %d Bytes\n",
        avg_halo_size/comm_size);
  }
}

/*******************************************************************************
 * Create map-specific halo exchange tables
 *******************************************************************************/

void op_halo_permap_create() {

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  /* --------Step 1: Decide which maps will do partial halo exchange ----------*/

  int *total_halo_sizes = (int *)calloc(OP_set_index, sizeof(int));
  int *map_halo_sizes = (int *)calloc(OP_map_index,sizeof(int));

  //Total halo size for each set
  for (int i = 0; i < OP_set_index; i++)
    total_halo_sizes[i] = OP_set_list[i]->nonexec_size+OP_set_list[i]->exec_size;

  //See how many map elements point outside of partition (incl. duplicates)
  for (int i = 0; i < OP_map_index; i++) {
    op_map map = OP_map_list[i];
    for (int e=map->from->core_size;
         e<map->from->size+map->from->exec_size;
         e++) {
      for (int j = 0; j < map->dim; j++)
        if (map->map[e*map->dim+j] >= map->to->size) map_halo_sizes[i]++;
    }
  }

  int *reduced_total_halo_sizes = (int *)calloc(OP_set_index, sizeof(int));
  int *reduced_map_halo_sizes = (int *)calloc(OP_map_index,sizeof(int));
  MPI_Allreduce(total_halo_sizes, reduced_total_halo_sizes,
                OP_set_index, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(map_halo_sizes, reduced_map_halo_sizes,
                OP_map_index, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  OP_map_partial_exchange = (int *)malloc(OP_map_index*sizeof(int));
  for (int i = 0; i < OP_map_index; i++) {
    OP_map_partial_exchange[i] = 0;//(double)reduced_map_halo_sizes[i] <
                      //(double)reduced_total_halo_sizes[OP_map_list[i]->to->index]*0.3;
    //if (rank == 0 && OP_diags>1) printf("Mapping %s partially exchanged: %d (%d < 0.3*%d)\n", OP_map_list[i]->name, OP_map_partial_exchange[i], reduced_map_halo_sizes[i], reduced_total_halo_sizes[OP_map_list[i]->to->index]);
  }
  free(reduced_total_halo_sizes);
  free(reduced_map_halo_sizes);
  free(total_halo_sizes);
  free(map_halo_sizes);

  /* --------Step 2: go through maps, determine import subset -----------------*/
  OP_import_nonexec_permap = (halo_list *)malloc(OP_map_index *
                                                      sizeof(halo_list));
  OP_export_nonexec_permap = (halo_list *)malloc(OP_map_index *
                                                      sizeof(halo_list));
  int **import_sizes2 = (int **)malloc(OP_map_index*sizeof(int*));
  int **export_sizes2 = (int **)malloc(OP_map_index*sizeof(int*));

  for (int i = 0; i < OP_map_index; i++) {
    if (OP_map_partial_exchange[i]) {
      OP_import_nonexec_permap[i] = (halo_list)malloc(sizeof(halo_list_core));
      OP_export_nonexec_permap[i] = (halo_list)malloc(sizeof(halo_list_core));
    }
  }

  set_import_buffer_size = (int *)calloc(OP_set_index, sizeof(int));
  for (int i = 0; i < OP_map_index; i++) {
    if (!OP_map_partial_exchange[i]) continue;
    op_map map = OP_map_list[i];
    OP_import_nonexec_permap[i]->set = map->to;
    OP_import_nonexec_permap[i]->size = 0;

    //
    // Merge exec and non-exec neighbors for the target set (import halo)
    //
    OP_import_nonexec_permap[i]->ranks_size = 0;
    int total = (OP_import_exec_list[map->to->index]->ranks_size + OP_import_nonexec_list[map->to->index]->ranks_size);
    OP_import_nonexec_permap[i]->ranks = (int*)calloc(total, sizeof(int));
    for (int j = 0; j < total; j++) {
      int merge = j < OP_import_exec_list[map->to->index]->ranks_size ?
        OP_import_exec_list[map->to->index]->ranks[j] : OP_import_nonexec_list[map->to->index]->ranks[j-OP_import_exec_list[map->to->index]->ranks_size];
      int found = 0;
      for (int k = 0; k < OP_import_nonexec_permap[i]->ranks_size; k++) {
        if (merge == OP_import_nonexec_permap[i]->ranks[k]) {found = 1; break;}
      }
      if (!found) OP_import_nonexec_permap[i]->ranks[OP_import_nonexec_permap[i]->ranks_size++] = merge;
    }
    quickSort(OP_import_nonexec_permap[i]->ranks, 0 , OP_import_nonexec_permap[i]->ranks_size-1);

    //
    // Count how many we will actually need from each of them for this particular map
    //
    OP_import_nonexec_permap[i]->disps = (int*)calloc(OP_import_nonexec_permap[i]->ranks_size, sizeof(int));
    OP_import_nonexec_permap[i]->sizes = (int*)calloc(OP_import_nonexec_permap[i]->ranks_size, sizeof(int));
    import_sizes2[i] = (int*)calloc(OP_import_nonexec_permap[i]->ranks_size, sizeof(int));

    //Create flag array: -1 for halo elements that are not eccessed by this map, gbl partition ID for elements that are
    int *scratch = (int *)malloc((map->to->exec_size + map->to->nonexec_size) *sizeof(int));
    for (int j = 0; j < map->to->exec_size + map->to->nonexec_size; j++) {
      scratch[j] = -1;
    }
    for (int e=map->from->core_size;
         e<map->from->size+map->from->exec_size;
         e++) {
      for (int j = 0; j < map->dim; j++) {
        //I know based on index whether it's exec or nonzexec halo region, one less search!
        if (map->map[e*map->dim+j] >= map->to->size) {
          int target_partition = 0;
          if (map->map[e*map->dim+j] < map->to->size+map->to->exec_size) {
            int index = map->map[e*map->dim+j] - map->to->size;
            while(target_partition <  OP_import_exec_list[map->to->index]->ranks_size-1 && index >= OP_import_exec_list[map->to->index]->disps[target_partition+1]) target_partition++;
            if (!(index >= OP_import_exec_list[map->to->index]->disps[target_partition] && index < OP_import_exec_list[map->to->index]->disps[target_partition] + OP_import_exec_list[map->to->index]->sizes[target_partition])) printf("ERROR:  exec index out of bounds of halo region! part idx %d/%d, index: %d %d-%d\n", target_partition, OP_import_exec_list[map->to->index]->ranks_size, index, OP_import_exec_list[map->to->index]->disps[target_partition], OP_import_exec_list[map->to->index]->sizes[target_partition]);
            target_partition = OP_import_exec_list[map->to->index]->ranks[target_partition];
          }
          else {
            int index = map->map[e*map->dim+j] - map->to->size - map->to->exec_size;
            while(target_partition <  OP_import_nonexec_list[map->to->index]->ranks_size-1 && index >= OP_import_nonexec_list[map->to->index]->disps[target_partition+1]) target_partition++;
            if (!(index >= OP_import_nonexec_list[map->to->index]->disps[target_partition] && index < OP_import_nonexec_list[map->to->index]->disps[target_partition] + OP_import_nonexec_list[map->to->index]->sizes[target_partition])) printf("ERROR: nonexec index out of bounds of halo region! part idx %d/%d, index: %d %d-%d\n", target_partition, OP_import_nonexec_list[map->to->index]->ranks_size, index, OP_import_nonexec_list[map->to->index]->disps[target_partition], OP_import_nonexec_list[map->to->index]->sizes[target_partition]);
            target_partition = OP_import_nonexec_list[map->to->index]->ranks[target_partition];
          }
          scratch[map->map[e*map->dim+j] - map->to->size] = target_partition;
        }
      }
    }
    //Find which index the partition ID is at in the permap halo list, and add to import size for that source
    for (int j = 0; j < map->to->exec_size + map->to->nonexec_size; j++) {
      if (scratch[j]>=0) {
        int target = linear_search(OP_import_nonexec_permap[i]->ranks, scratch[j], 0, OP_import_nonexec_permap[i]->ranks_size-1);
        if (j < map->to->exec_size) {
          int target_ori = linear_search(OP_import_exec_list[map->to->index]->ranks, scratch[j], 0, OP_import_exec_list[map->to->index]->ranks_size-1);
          if (target_ori==-1 || !(j>=OP_import_exec_list[map->to->index]->disps[target_ori] && j < OP_import_exec_list[map->to->index]->disps[target_ori] + OP_import_exec_list[map->to->index]->sizes[target_ori])) printf("ERROR: scratch exec position out of range\n");
        } else {
          int target_ori = linear_search(OP_import_nonexec_list[map->to->index]->ranks, scratch[j], 0, OP_import_nonexec_list[map->to->index]->ranks_size-1);
          if (target_ori==-1 || !(j-map->to->exec_size>=OP_import_nonexec_list[map->to->index]->disps[target_ori] && j < OP_import_nonexec_list[map->to->index]->disps[target_ori] + OP_import_nonexec_list[map->to->index]->sizes[target_ori])) printf("ERROR: scratch exec position out of range\n");
        }
        scratch[j] = target;
        OP_import_nonexec_permap[i]->sizes[target]++;
      }
    }

    //Cumulative sum to determine total size
    OP_import_nonexec_permap[i]->disps[0] = 0;
    for (int j = 1; j < OP_import_nonexec_permap[i]->ranks_size; j++) {
      OP_import_nonexec_permap[i]->disps[j] = OP_import_nonexec_permap[i]->disps[j-1] + OP_import_nonexec_permap[i]->sizes[j-1];
      OP_import_nonexec_permap[i]->sizes[j-1] = 0;
    }
    OP_import_nonexec_permap[i]->size = OP_import_nonexec_permap[i]->disps[OP_import_nonexec_permap[i]->ranks_size-1] +
                                        OP_import_nonexec_permap[i]->sizes[OP_import_nonexec_permap[i]->ranks_size-1];
    OP_import_nonexec_permap[i]->sizes[OP_import_nonexec_permap[i]->ranks_size-1] = 0;

    set_import_buffer_size[map->to->index] = MAX(set_import_buffer_size[map->to->index], OP_import_nonexec_permap[i]->size);

    //
    // Populate halo lists with offsets into current halo segment (exec/nonexec and source partition)
    //
    OP_import_nonexec_permap[i]->list = (int *)malloc(OP_import_nonexec_permap[i]->size * sizeof(int));
    for (int j = 0; j < map->to->exec_size + map->to->nonexec_size; j++) {
      if (scratch[j]>=0) {
        int local_offset = -1;
        int target_partition = OP_import_nonexec_permap[i]->ranks[scratch[j]];
        if (j < map->to->exec_size) {
          int target_partition_idx = linear_search(OP_import_exec_list[map->to->index]->ranks, target_partition, 0, OP_import_exec_list[map->to->index]->ranks_size-1);
          if (target_partition_idx == -1 || !(j>=OP_import_exec_list[map->to->index]->disps[target_partition_idx] && j < OP_import_exec_list[map->to->index]->disps[target_partition_idx] + OP_import_exec_list[map->to->index]->sizes[target_partition_idx])) printf("ERROR: population exec position out of range\n");
          local_offset = j - OP_import_exec_list[map->to->index]->disps[target_partition_idx];
          import_sizes2[i][scratch[j]]++;
        } else {
          int target_partition_idx = linear_search(OP_import_nonexec_list[map->to->index]->ranks, target_partition, 0, OP_import_nonexec_list[map->to->index]->ranks_size-1);
          if (target_partition_idx == -1 || !(j-map->to->exec_size>=OP_import_nonexec_list[map->to->index]->disps[target_partition_idx] && j < OP_import_nonexec_list[map->to->index]->disps[target_partition_idx] + OP_import_nonexec_list[map->to->index]->sizes[target_partition_idx])) printf("ERROR: scratch exec position out of range\n");
          local_offset = j - OP_import_nonexec_list[map->to->index]->disps[target_partition_idx] - OP_import_exec_list[map->to->index]->size;
        }
        OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[scratch[j]] + OP_import_nonexec_permap[i]->sizes[scratch[j]]] = local_offset;
        OP_import_nonexec_permap[i]->sizes[scratch[j]]++;
      }
    }
    free(scratch);

    //
    // Let the other ranks know how many elements we will need from their exec/nonexec halo regions
    //
    int *send_buffer = (int *)malloc(2 * OP_import_nonexec_permap[i]->ranks_size * sizeof(int));
    int *recv_buffer = (int *)malloc(2 * OP_import_nonexec_permap[i]->ranks_size * sizeof(int));
    MPI_Status *send_status = (MPI_Status *)malloc(OP_import_nonexec_permap[i]->ranks_size * sizeof(MPI_Status));
    MPI_Status *recv_status = (MPI_Status *)malloc(OP_import_nonexec_permap[i]->ranks_size * sizeof(MPI_Status));
    MPI_Request *send_request = (MPI_Request *)malloc(OP_import_nonexec_permap[i]->ranks_size * sizeof(MPI_Request));

    for (int j = 0; j < OP_import_nonexec_permap[i]->ranks_size; j++) {
      send_buffer[2*j] = import_sizes2[i][j];
      send_buffer[2*j+1] = OP_import_nonexec_permap[i]->sizes[j] - import_sizes2[i][j];
      MPI_Isend(&send_buffer[2*j], 2, MPI_INT, OP_import_nonexec_permap[i]->ranks[j], 0, MPI_COMM_WORLD, &send_request[j]);
    }

    OP_export_nonexec_permap[i]->set = map->to;
    OP_export_nonexec_permap[i]->size = 0;
    //
    // Merge exec and non-exec neighbors for the target set (export halo)
    //
    OP_export_nonexec_permap[i]->ranks_size = 0;
    total = (OP_export_exec_list[map->to->index]->ranks_size + OP_export_nonexec_list[map->to->index]->ranks_size);
    OP_export_nonexec_permap[i]->ranks = (int*)calloc(total, sizeof(int));
    for (int j = 0; j < total; j++) {
      int merge = j < OP_export_exec_list[map->to->index]->ranks_size ?
        OP_export_exec_list[map->to->index]->ranks[j] : OP_export_nonexec_list[map->to->index]->ranks[j-OP_export_exec_list[map->to->index]->ranks_size];
      int found = 0;
      for (int k = 0; k < OP_export_nonexec_permap[i]->ranks_size; k++) {
        if (merge == OP_export_nonexec_permap[i]->ranks[k]) {found = 1; break;}
      }
      if (!found) OP_export_nonexec_permap[i]->ranks[OP_export_nonexec_permap[i]->ranks_size++] = merge;
    }
    quickSort(OP_export_nonexec_permap[i]->ranks, 0 , OP_export_nonexec_permap[i]->ranks_size-1);

    //
    // Receive sizes, allocate export lists
    //
    OP_export_nonexec_permap[i]->disps = (int*)calloc(OP_export_nonexec_permap[i]->ranks_size, sizeof(int));
    OP_export_nonexec_permap[i]->sizes = (int*)calloc(OP_export_nonexec_permap[i]->ranks_size, sizeof(int));
    export_sizes2[i] = (int*)calloc(OP_export_nonexec_permap[i]->ranks_size, sizeof(int));
    OP_export_nonexec_permap[i]->disps[0] = 0;
    for (int j = 0; j < OP_export_nonexec_permap[i]->ranks_size; j++) {
      MPI_Recv(&recv_buffer[2*j], 2, MPI_INT, OP_export_nonexec_permap[i]->ranks[j], 0, MPI_COMM_WORLD, &recv_status[j]);
      export_sizes2[i][j] = recv_buffer[2*j];
      OP_export_nonexec_permap[i]->sizes[j] = recv_buffer[2*j] + recv_buffer[2*j+1];
      if (j>0) OP_export_nonexec_permap[i]->disps[j] = OP_export_nonexec_permap[i]->disps[j-1] + OP_export_nonexec_permap[i]->sizes[j-1];
    }
    MPI_Waitall(OP_import_nonexec_permap[i]->ranks_size, send_request, send_status);
    free(send_buffer);
    free(recv_buffer);

    OP_export_nonexec_permap[i]->size = OP_export_nonexec_permap[i]->disps[OP_export_nonexec_permap[i]->ranks_size-1] +
                                        OP_export_nonexec_permap[i]->sizes[OP_export_nonexec_permap[i]->ranks_size-1];
    OP_export_nonexec_permap[i]->list = (int*)malloc(OP_export_nonexec_permap[i]->size * sizeof(int));

    //
    // Collapse import and export lists (remove 0 size destinations)
    //
    int new_size = 0;
    for (int j = 0; j < OP_import_nonexec_permap[i]->ranks_size; j++) {
      if (OP_import_nonexec_permap[i]->sizes[j] > 0) {
        OP_import_nonexec_permap[i]->sizes[new_size]  = OP_import_nonexec_permap[i]->sizes[j];
        import_sizes2[i][new_size] = import_sizes2[i][j];
        OP_import_nonexec_permap[i]->disps[new_size]  = OP_import_nonexec_permap[i]->disps[j];
        OP_import_nonexec_permap[i]->ranks[new_size]  = OP_import_nonexec_permap[i]->ranks[j];
        new_size++;
      }
    }
    OP_import_nonexec_permap[i]->ranks_size = new_size;

    new_size = 0;
    for (int j = 0; j < OP_export_nonexec_permap[i]->ranks_size; j++) {
      if (OP_export_nonexec_permap[i]->sizes[j] > 0) {
        OP_export_nonexec_permap[i]->sizes[new_size]  = OP_export_nonexec_permap[i]->sizes[j];
        export_sizes2[i][new_size] = export_sizes2[i][j];
        OP_export_nonexec_permap[i]->disps[new_size]  = OP_export_nonexec_permap[i]->disps[j];
        OP_export_nonexec_permap[i]->ranks[new_size]  = OP_export_nonexec_permap[i]->ranks[j];
        new_size++;
      }
    }
    OP_export_nonexec_permap[i]->ranks_size = new_size;

    //
    // Send and Receive export lists, substitute local indices
    //
    for (int j = 0; j < OP_import_nonexec_permap[i]->ranks_size; j++) {
      MPI_Isend(&OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[j]], OP_import_nonexec_permap[i]->sizes[j],
                MPI_INT, OP_import_nonexec_permap[i]->ranks[j], 1, MPI_COMM_WORLD, &send_request[j]);
    }
    for (int j = 0; j < OP_export_nonexec_permap[i]->ranks_size; j++) {
      MPI_Recv(&OP_export_nonexec_permap[i]->list[OP_export_nonexec_permap[i]->disps[j]], OP_export_nonexec_permap[i]->sizes[j],
                MPI_INT, OP_export_nonexec_permap[i]->ranks[j], 1, MPI_COMM_WORLD, &recv_status[j]);
      for (int k = 0; k < export_sizes2[i][j]; k++) {
        int element = OP_export_nonexec_permap[i]->list[OP_export_nonexec_permap[i]->disps[j]+k];
        int source_partition_idx = linear_search(OP_export_exec_list[map->to->index]->ranks,
                                                 OP_export_nonexec_permap[i]->ranks[j], 0,
                                                 OP_export_exec_list[map->to->index]->ranks_size-1);
        if (source_partition_idx == -1) printf("ERROR: exec source partition for export not found\n");
        OP_export_nonexec_permap[i]->list[OP_export_nonexec_permap[i]->disps[j]+k] = OP_export_exec_list[map->to->index]->list[
                                                        OP_export_exec_list[map->to->index]->disps[source_partition_idx] + element];
      }
      for (int k = export_sizes2[i][j]; k < OP_export_nonexec_permap[i]->sizes[j]; k++) {
        int element = OP_export_nonexec_permap[i]->list[OP_export_nonexec_permap[i]->disps[j]+k];
        int source_partition_idx = linear_search(OP_export_nonexec_list[map->to->index]->ranks,
                                                 OP_export_nonexec_permap[i]->ranks[j], 0,
                                                 OP_export_nonexec_list[map->to->index]->ranks_size-1);
        if (source_partition_idx == -1) printf("ERROR: nonexec source partition for export not found\n");
        OP_export_nonexec_permap[i]->list[OP_export_nonexec_permap[i]->disps[j]+k] = OP_export_nonexec_list[map->to->index]->list[
                                                        OP_export_nonexec_list[map->to->index]->disps[source_partition_idx] + element];
      }
    }
    MPI_Waitall(OP_import_nonexec_permap[i]->ranks_size, send_request, send_status);
    for (int j = 0; j < OP_import_nonexec_permap[i]->ranks_size; j++) {
      for (int k = 0; k < import_sizes2[i][j]; k++) {
        int element = OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[j]+k];
        int source_partition_idx = linear_search(OP_import_exec_list[map->to->index]->ranks,
                                                 OP_import_nonexec_permap[i]->ranks[j], 0,
                                                 OP_import_exec_list[map->to->index]->ranks_size-1);
        if (source_partition_idx == -1) printf("ERROR: exec source partition for import not found\n");
        OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[j]+k] = map->to->size + OP_import_exec_list[map->to->index]->disps[source_partition_idx] + element;
      }
      for (int k = import_sizes2[i][j]; k < OP_import_nonexec_permap[i]->sizes[j]; k++) {
        int element = OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[j]+k];
        int source_partition_idx = linear_search(OP_import_nonexec_list[map->to->index]->ranks,
                                                 OP_import_nonexec_permap[i]->ranks[j], 0,
                                                 OP_import_nonexec_list[map->to->index]->ranks_size-1);
        if (source_partition_idx == -1) printf("ERROR: nonexec source partition for import not found\n");
        OP_import_nonexec_permap[i]->list[OP_import_nonexec_permap[i]->disps[j]+k] = map->to->size + map->to->exec_size +
                                                        OP_import_nonexec_list[map->to->index]->disps[source_partition_idx] + element;
      }
    }

    free(recv_status);
    free(send_status);
    free(send_request);
  }

  //
  // resize mpi_buffers to accommodate import data before scattering to actual positions
  //
  for (int i = 0; i < OP_set_index; i++) {
    if (set_import_buffer_size[i] == 0) continue;
    op_dat_entry *item;
    TAILQ_FOREACH(item, &OP_dat_list, entries) {
      op_dat dat = item->dat;
      halo_list nonexec_e_list = OP_export_nonexec_list[i];
      ((op_mpi_buffer)(dat->mpi_buffer))->buf_nonexec = (char *)xrealloc(((op_mpi_buffer)(dat->mpi_buffer))->buf_nonexec,
                                                                         (nonexec_e_list->size + set_import_buffer_size[i])*dat->size);
    }
  }

  //
  // Sanity checks
  //
  for (int i = 0; i < OP_map_index; i++) {
    if (OP_map_partial_exchange[i] == 0) continue;
    op_map map = OP_map_list[i];
    for (int e=map->from->core_size;
         e<map->from->size+map->from->exec_size;
         e++) {
      for (int j = 0; j < map->dim; j++)
        if (map->map[e*map->dim+j] >= map->to->size) {
          int idx = linear_search(OP_import_nonexec_permap[i]->list, map->map[e*map->dim+j], 0, OP_import_nonexec_permap[i]->size-1);
          if (idx == -1) {
            printf("ERROR: map element not found in partial halo exchange list!\n");
          }
        }
    }
  }
  free(import_sizes2);
  free(export_sizes2);
}

/*******************************************************************************
 * Routine to Clean-up all MPI halos(called at the end of an OP2 MPI application)
 *******************************************************************************/

void op_halo_destroy()
{
  //remove halos from op_dats
  op_dat_entry *item;
  TAILQ_FOREACH(item, &OP_dat_list, entries) {
    op_dat dat = item->dat;
    dat->data =(char *)xrealloc(dat->data,dat->set->size*dat->size);
  }

  //free lists
  for(int s = 0; s< OP_set_index; s++){
    op_set set=OP_set_list[s];

    free(OP_import_exec_list[set->index]->ranks);
    free(OP_import_exec_list[set->index]->disps);
    free(OP_import_exec_list[set->index]->sizes);
    free(OP_import_exec_list[set->index]->list);
    free(OP_import_exec_list[set->index]);

    free(OP_import_nonexec_list[set->index]->ranks);
    free(OP_import_nonexec_list[set->index]->disps);
    free(OP_import_nonexec_list[set->index]->sizes);
    free(OP_import_nonexec_list[set->index]->list);
    free(OP_import_nonexec_list[set->index]);

    free(OP_export_exec_list[set->index]->ranks);
    free(OP_export_exec_list[set->index]->disps);
    free(OP_export_exec_list[set->index]->sizes);
    free(OP_export_exec_list[set->index]->list);
    free(OP_export_exec_list[set->index]);

    free(OP_export_nonexec_list[set->index]->ranks);
    free(OP_export_nonexec_list[set->index]->disps);
    free(OP_export_nonexec_list[set->index]->sizes);
    free(OP_export_nonexec_list[set->index]->list);
    free(OP_export_nonexec_list[set->index]);

  }
  free(OP_import_exec_list);free(OP_import_nonexec_list);
  free(OP_export_exec_list);free(OP_export_nonexec_list);

  item = NULL;
  TAILQ_FOREACH(item, &OP_dat_list, entries) {
    op_dat dat = item->dat;
    free(((op_mpi_buffer)(dat->mpi_buffer))->buf_exec);
    free(((op_mpi_buffer)(dat->mpi_buffer))->buf_nonexec);
    free(((op_mpi_buffer)(dat->mpi_buffer))->s_req);
    free(((op_mpi_buffer)(dat->mpi_buffer))->r_req);
  }

  MPI_Comm_free(&OP_MPI_WORLD);
}

/*******************************************************************************
 * Routine to set the dirty bit for an MPI Halo after halo exchange
 *******************************************************************************/

static void set_dirtybit(op_arg* arg, int hd)
{
  op_dat dat = arg->dat;

  if((arg->opt==1) && (arg->argtype == OP_ARG_DAT) &&
    (arg->acc == OP_INC || arg->acc == OP_WRITE || arg->acc == OP_RW)) {
    dat->dirtybit = 1;
    dat->dirty_hd = hd;
  }
}


void op_mpi_reduce_combined(op_arg* args, int nargs) {
  op_timers_core(&c1, &t1);
  int nreductions = 0;
  for (int i = 0; i < nargs; i++) {
    if (args[i].argtype == OP_ARG_GBL && args[i].acc != OP_READ) nreductions++;
  }
  op_arg *arg_list = (op_arg*)malloc(nreductions*sizeof(op_arg));
  nreductions = 0;
  int nbytes = 0;
  for (int i = 0; i < nargs; i++) {
    if (args[i].argtype == OP_ARG_GBL && args[i].acc != OP_READ) {
      arg_list[nreductions++] = args[i];
      nbytes += args[i].size;
    }
  }

  char *data = (char *)malloc(nbytes*sizeof(char));
  int char_counter = 0;
  for (int i = 0; i < nreductions; i++) {
    for (int j = 0; j < arg_list[i].size; j++)
      data[char_counter++] = arg_list[i].data[j];
  }

  int comm_size, comm_rank;
  MPI_Comm_size(OP_MPI_WORLD, &comm_size);
  MPI_Comm_rank(OP_MPI_WORLD, &comm_rank);
  char *result = (char *)malloc(comm_size*nbytes*sizeof(char));
  MPI_Allgather(data,   nbytes, MPI_CHAR,
                result, nbytes, MPI_CHAR,
                OP_MPI_WORLD);

  char_counter = 0;
  for (int i = 0; i < nreductions; i++) {
    if (strcmp(arg_list[i].type,"double")==0 || strcmp(arg_list[i].type,"r8")==0) {
      double *output = (double *)arg_list[i].data;
      for (int rank = 0; rank < comm_size; rank++) {
        if (rank != comm_rank){
          if (arg_list[i].acc == OP_INC) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] += ((double*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MIN) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] < ((double*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((double*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MAX) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] > ((double*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((double*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_WRITE) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] != 0.0 ? output[j] : ((double*)(result+char_counter+nbytes*rank))[j];
            }
          }
        }
      }
    }
    if (strcmp(arg_list[i].type,"float")==0 || strcmp(arg_list[i].type,"r4")==0) {
      float *output = (float *)arg_list[i].data;
      for (int rank = 0; rank < comm_size; rank++) {
        if (rank != comm_rank){
          if (arg_list[i].acc == OP_INC) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] += ((float*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MIN) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] < ((float*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((float*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MAX) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] > ((float*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((float*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_WRITE) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] != 0.0 ? output[j] : ((float*)(result+char_counter+nbytes*rank))[j];
            }
          }
        }
      }
    }
    if (strcmp(arg_list[i].type,"int")==0 || strcmp(arg_list[i].type,"i4")==0) {
      int *output = (int *)arg_list[i].data;
      for (int rank = 0; rank < comm_size; rank++) {
        if (rank != comm_rank){
          if (arg_list[i].acc == OP_INC) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] += ((int*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MIN) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] < ((int*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((int*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MAX) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] > ((int*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((int*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_WRITE) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] != 0.0 ? output[j] : ((int*)(result+char_counter+nbytes*rank))[j];
            }
          }
        }
      }
    }
    if (strcmp(arg_list[i].type,"bool")==0 || strcmp(arg_list[i].type,"logical")==0) {
      bool *output = (bool *)arg_list[i].data;
      for (int rank = 0; rank < comm_size; rank++) {
        if (rank != comm_rank){
          if (arg_list[i].acc == OP_INC) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] += ((bool*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MIN) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] < ((bool*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((bool*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_MAX) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] > ((bool*)(result+char_counter+nbytes*rank))[j] ? output[j] : ((bool*)(result+char_counter+nbytes*rank))[j];
            }
          } else if (arg_list[i].acc == OP_WRITE) {
            for (int j = 0; j < arg_list[i].dim; j++) {
              output[j] = output[j] != 0.0 ? output[j] : ((bool*)(result+char_counter+nbytes*rank))[j];
            }
          }
        }
      }
    }
    char_counter += arg_list[i].size;
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
  free(arg_list);
  free(data);
  free(result);
}

void op_mpi_reduce_float(op_arg* arg, float* data)
{
  (void)data;
  op_timers_core(&c1, &t1);
  if(arg->argtype == OP_ARG_GBL && arg->acc != OP_READ)
  {
    float result_static;
    float *result;
    if (arg->dim > 1 && arg->acc != OP_WRITE) result = (float *) calloc (arg->dim, sizeof (float));
    else result = &result_static;

    if(arg->acc == OP_INC)//global reduction
    {
      MPI_Allreduce((float *)arg->data, result, arg->dim, MPI_FLOAT,
          MPI_SUM, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(float)*arg->dim);
    }
    else if(arg->acc == OP_MAX)//global maximum
    {
      MPI_Allreduce((float *)arg->data, result, arg->dim, MPI_FLOAT,
          MPI_MAX, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(float)*arg->dim);;
    }
    else if(arg->acc == OP_MIN)//global minimum
    {
      MPI_Allreduce((float *)arg->data, result, arg->dim, MPI_FLOAT,
          MPI_MIN, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(float)*arg->dim);
    }
    else if(arg->acc == OP_WRITE)//any
    {
      int size;
      MPI_Comm_size(OP_MPI_WORLD, &size);
      result = (float *) calloc (arg->dim*size, sizeof (float));
      MPI_Allgather((float *)arg->data, arg->dim, MPI_FLOAT,
                    result, arg->dim, MPI_FLOAT,
                    OP_MPI_WORLD);
      for (int i = 1; i < size; i++) {
        for (int j = 0; j < arg->dim; j++) {
          if (result[i*arg->dim+j] != 0.0f)
            result[j] = result[i*arg->dim+j];
        }
      }
      memcpy(arg->data, result, sizeof(float)*arg->dim);
      if (arg->dim == 1) free(result);
    }
    if (arg->dim > 1) free (result);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

void op_mpi_reduce_double(op_arg* arg, double* data)
{
  (void)data;
  op_timers_core(&c1, &t1);
  if(arg->argtype == OP_ARG_GBL && arg->acc != OP_READ)
  {
    double result_static;
    double *result;
    if (arg->dim > 1 && arg->acc != OP_WRITE) result = (double *) calloc (arg->dim, sizeof (double));
    else result = &result_static;

    if(arg->acc == OP_INC)//global reduction
    {
      MPI_Allreduce((double *)arg->data, result, arg->dim, MPI_DOUBLE,
          MPI_SUM, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(double)*arg->dim);
    }
    else if(arg->acc == OP_MAX)//global maximum
    {
      MPI_Allreduce((double *)arg->data, result, arg->dim, MPI_DOUBLE,
          MPI_MAX, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(double)*arg->dim);;
    }
    else if(arg->acc == OP_MIN)//global minimum
    {
      MPI_Allreduce((double *)arg->data, result, arg->dim, MPI_DOUBLE,
          MPI_MIN, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(double)*arg->dim);
    }
    else if(arg->acc == OP_WRITE)//any
    {
      int size;
      MPI_Comm_size(OP_MPI_WORLD, &size);
      result = (double *) calloc (arg->dim*size, sizeof (double));
      MPI_Allgather((double *)arg->data, arg->dim, MPI_DOUBLE,
                    result, arg->dim, MPI_DOUBLE,
                    OP_MPI_WORLD);
      for (int i = 1; i < size; i++) {
        for (int j = 0; j < arg->dim; j++) {
          if (result[i*arg->dim+j] != 0.0)
            result[j] = result[i*arg->dim+j];
        }
      }
      memcpy(arg->data, result, sizeof(double)*arg->dim);
      if (arg->dim == 1) free(result);
    }
    if (arg->dim > 1) free (result);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

void op_mpi_reduce_int(op_arg* arg, int* data)
{
  (void)data;
  op_timers_core(&c1, &t1);
  if(arg->argtype == OP_ARG_GBL && arg->acc != OP_READ)
  {
    int result_static;
    int *result;
    if (arg->dim > 1 && arg->acc != OP_WRITE) result = (int *) calloc (arg->dim, sizeof (int));
    else result = &result_static;

    if(arg->acc == OP_INC)//global reduction
    {
      MPI_Allreduce((int *)arg->data, result, arg->dim, MPI_INT,
          MPI_SUM, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(int)*arg->dim);
    }
    else if(arg->acc == OP_MAX)//global maximum
    {
      MPI_Allreduce((int *)arg->data, result, arg->dim, MPI_INT,
          MPI_MAX, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(int)*arg->dim);;
    }
    else if(arg->acc == OP_MIN)//global minimum
    {
      MPI_Allreduce((int *)arg->data, result, arg->dim, MPI_INT,
          MPI_MIN, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(int)*arg->dim);
    }
    else if(arg->acc == OP_WRITE)//any
    {
      int size;
      MPI_Comm_size(OP_MPI_WORLD, &size);
      result = (int *) calloc (arg->dim*size, sizeof (int));
      MPI_Allgather((int *)arg->data, arg->dim, MPI_INT,
                    result, arg->dim, MPI_INT,
                    OP_MPI_WORLD);
      for (int i = 1; i < size; i++) {
        for (int j = 0; j < arg->dim; j++) {
          if (result[i*arg->dim+j] != 0)
            result[j] = result[i*arg->dim+j];
        }
      }
      memcpy(arg->data, result, sizeof(int)*arg->dim);
      if (arg->dim == 1) free(result);
    }
    if (arg->dim > 1) free (result);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

void op_mpi_reduce_bool(op_arg* arg, bool* data)
{
  (void)data;
  op_timers_core(&c1, &t1);
  if(arg->argtype == OP_ARG_GBL && arg->acc != OP_READ)
  {
    bool result_static;
    bool *result;
    if (arg->dim > 1) result = (bool *) calloc (arg->dim, sizeof (bool));
    else result = &result_static;

    if(arg->acc == OP_INC)//global reduction
    {
      MPI_Allreduce((bool *)arg->data, result, arg->dim, MPI_CHAR,
          MPI_SUM, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(bool)*arg->dim);
    }
    else if(arg->acc == OP_MAX)//global maximum
    {
      MPI_Allreduce((bool *)arg->data, result, arg->dim, MPI_CHAR,
          MPI_MAX, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(bool)*arg->dim);;
    }
    else if(arg->acc == OP_MIN)//global minimum
    {
      MPI_Allreduce((bool *)arg->data, result, arg->dim, MPI_CHAR,
          MPI_MIN, OP_MPI_WORLD);
      memcpy(arg->data, result, sizeof(bool)*arg->dim);
    }
    else if(arg->acc == OP_WRITE)//any
    {
      int size;
      MPI_Comm_size(OP_MPI_WORLD, &size);
      result = (bool *) calloc (arg->dim*size, sizeof (bool));
      MPI_Allgather((int *)arg->data, arg->dim, MPI_CHAR,
                    result, arg->dim, MPI_CHAR,
                    OP_MPI_WORLD);
      for (int i = 1; i < size; i++) {
        for (int j = 0; j < arg->dim; j++) {
          if (result[i*arg->dim+j] != false)
            result[j] = result[i*arg->dim+j];
        }
      }
      memcpy(arg->data, result, sizeof(bool)*arg->dim);
      if (arg->dim == 1) free(result);
    }
    if (arg->dim > 1) free (result);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

/*******************************************************************************
 * Routine to get a copy of the data held in a distributed op_dat
 *******************************************************************************/

op_dat op_mpi_get_data(op_dat dat)
{
  //create new communicator for fetching
  int my_rank, comm_size;
  MPI_Comm_rank(OP_MPI_WORLD, &my_rank);
  MPI_Comm_size(OP_MPI_WORLD, &comm_size);

  //
  //make a copy of the distributed op_dat on to a distributed temporary op_dat
  //
  op_dat temp_dat = (op_dat) xmalloc(sizeof(op_dat_core));
  char *data = (char *)xmalloc(dat->set->size*dat->size);
  memcpy(data, dat->data, dat->set->size*dat->size);

  //
  //use orig_part_range to fill in OP_part_list[set->index]->elem_part with
  //original partitioning information
  //
  for(int i = 0; i < dat->set->size; i++)
  {
    int local_index;
    OP_part_list[dat->set->index]->elem_part[i] =
      get_partition(OP_part_list[dat->set->index]->g_index[i],
          orig_part_range[dat->set->index], &local_index, comm_size);
  }


  halo_list pe_list;
  halo_list pi_list;

  //
  //create export list
  //
  part p= OP_part_list[dat->set->index];
  int count = 0;int cap = 1000;
  int *temp_list = (int *)xmalloc(cap*sizeof(int));

  for(int i = 0; i < dat->set->size; i++)
  {
    if(p->elem_part[i] != my_rank)
    {
      if(count>=cap)
      {
        cap = cap*2;
        temp_list = (int *)xrealloc(temp_list, cap*sizeof(int));
      }
      temp_list[count++] = p->elem_part[i];
      temp_list[count++] = i;
    }
  }

  pe_list = (halo_list) xmalloc(sizeof(halo_list_core));
  create_export_list(dat->set, temp_list, pe_list, count, comm_size, my_rank);
  free(temp_list);


  //
  //create import list
  //
  int *neighbors, *sizes;
  int ranks_size;

  //-----Discover neighbors-----
  ranks_size = 0;
  neighbors = (int *)xmalloc(comm_size*sizeof(int));
  sizes = (int *)xmalloc(comm_size*sizeof(int));

  find_neighbors_set(pe_list, neighbors, sizes, &ranks_size,
      my_rank, comm_size, OP_MPI_WORLD);
  MPI_Request request_send[pe_list->ranks_size];

  int* rbuf;
  cap = 0; count = 0;

  for(int i=0; i<pe_list->ranks_size; i++) {
    int* sbuf = &pe_list->list[pe_list->disps[i]];
    MPI_Isend( sbuf,  pe_list->sizes[i],  MPI_INT, pe_list->ranks[i], 1,
        OP_MPI_WORLD, &request_send[i] );
  }

  for(int i=0; i< ranks_size; i++) cap = cap + sizes[i];
  temp_list = (int *)xmalloc(cap*sizeof(int));

  for(int i=0; i<ranks_size; i++) {
    rbuf = (int *)xmalloc(sizes[i]*sizeof(int));
    MPI_Recv(rbuf, sizes[i], MPI_INT, neighbors[i], 1, OP_MPI_WORLD,
        MPI_STATUSES_IGNORE );
    memcpy(&temp_list[count],(void *)&rbuf[0],sizes[i]*sizeof(int));
    count = count + sizes[i];
    free(rbuf);
  }

  MPI_Waitall(pe_list->ranks_size,request_send, MPI_STATUSES_IGNORE );
  pi_list = (halo_list) xmalloc(sizeof(halo_list_core));
  create_import_list(dat->set, temp_list, pi_list, count,
      neighbors, sizes, ranks_size, comm_size, my_rank);


  //
  //migrate the temp "data" array to the original MPI ranks
  //

  //prepare bits of the data array to be exported
  char** sbuf_char = (char **)xmalloc(pe_list->ranks_size*sizeof(char *));

  for(int i=0; i < pe_list->ranks_size; i++) {
    sbuf_char[i] = (char *)xmalloc(pe_list->sizes[i]*dat->size);
    for(int j = 0; j<pe_list->sizes[i]; j++)
    {
      int index = pe_list->list[pe_list->disps[i]+j];
      memcpy(&sbuf_char[i][j*dat->size],
          (void *)&data[dat->size*(index)],dat->size);
    }
    MPI_Isend(sbuf_char[i], dat->size*pe_list->sizes[i],
        MPI_CHAR, pe_list->ranks[i],
        dat->index, OP_MPI_WORLD, &request_send[i]);
  }

  char *rbuf_char = (char *)xmalloc(dat->size*pi_list->size);
  for(int i=0; i < pi_list->ranks_size; i++) {
    MPI_Recv(&rbuf_char[pi_list->disps[i]*dat->size],dat->size*pi_list->sizes[i],
        MPI_CHAR, pi_list->ranks[i], dat->index, OP_MPI_WORLD,
        MPI_STATUSES_IGNORE);
  }

  MPI_Waitall(pe_list->ranks_size,request_send, MPI_STATUSES_IGNORE );
  for(int i=0; i < pe_list->ranks_size; i++) free(sbuf_char[i]);
  free(sbuf_char);

  //delete the data entirs that has been sent and create a
  //modified data array
  char* new_dat = (char *)xmalloc(dat->size*(dat->set->size+pi_list->size));

  count = 0;
  for(int i = 0; i < dat->set->size;i++)//iterate over old set size
  {
    if(OP_part_list[dat->set->index]->elem_part[i] == my_rank)
    {
      memcpy(&new_dat[count*dat->size],
          (void *)&data[dat->size*i],dat->size);
      count++;
    }
  }

  memcpy(&new_dat[count*dat->size],(void *)rbuf_char,dat->size*pi_list->size);
  count = count+pi_list->size;
  new_dat = (char *)xrealloc(new_dat,dat->size*count);
  free(rbuf_char);
  free(data);
  data = new_dat;

  //
  //make a copy of the original g_index and migrate that also to the original
  //MPI process
  //
  //prepare bits of the original g_index array to be exported
  int** sbuf = (int **)xmalloc(pe_list->ranks_size*sizeof(int *));

  //send original g_index values to relevant mpi processes
  for(int i=0; i < pe_list->ranks_size; i++) {
    sbuf[i] = (int *)xmalloc(pe_list->sizes[i]*sizeof(int));
    for(int j = 0; j<pe_list->sizes[i]; j++)
    {
      sbuf[i][j] = OP_part_list[dat->set->index]->
        g_index[pe_list->list[pe_list->disps[i]+j]];
    }
    MPI_Isend(sbuf[i],  pe_list->sizes[i],
        MPI_INT, pe_list->ranks[i],
        dat->index, OP_MPI_WORLD, &request_send[i]);
  }

  rbuf = (int *)xmalloc(sizeof(int)*pi_list->size);

  //receive original g_index values from relevant mpi processes
  for(int i=0; i < pi_list->ranks_size; i++) {
    MPI_Recv(&rbuf[pi_list->disps[i]],pi_list->sizes[i],
        MPI_INT, pi_list->ranks[i], dat->index,
        OP_MPI_WORLD, MPI_STATUSES_IGNORE);
  }
  MPI_Waitall(pe_list->ranks_size,request_send, MPI_STATUSES_IGNORE );
  for(int i=0; i < pe_list->ranks_size; i++) free(sbuf[i]); free(sbuf);

  //delete the g_index entirs that has been sent and create a
  //modified g_index
  int* new_g_index = (int *)xmalloc(sizeof(int)*(dat->set->size+pi_list->size));

  count = 0;
  for(int i = 0; i < dat->set->size;i++)//iterate over old size of the g_index array
  {
    if(OP_part_list[dat->set->index]->elem_part[i] == my_rank)
    {
      new_g_index[count] = OP_part_list[dat->set->index]->g_index[i];
      count++;
    }
  }

  memcpy(&new_g_index[count],(void *)rbuf,sizeof(int)*pi_list->size);
  count = count+pi_list->size;
  new_g_index = (int *)xrealloc(new_g_index,sizeof(int)*count);
  free(rbuf);

  //
  //sort elements in temporaty data according to new_g_index
  //
  quickSort_dat(new_g_index,data, 0,count-1, dat->size);

  //cleanup
  free(pe_list->ranks);free(pe_list->disps);
  free(pe_list->sizes);free(pe_list->list);
  free(pe_list);
  free(pi_list->ranks);free(pi_list->disps);
  free(pi_list->sizes);free(pi_list->list);
  free(pi_list);
  free(new_g_index);

  //remember that the original set size is now given by count
  op_set set = (op_set) malloc(sizeof(op_set_core));
  set->index = dat->set->index;
  set->size  = count;
  set->name  = dat->set->name;

  temp_dat->index = dat->index;
  temp_dat->set = set;
  temp_dat->dim = dat->dim;
  temp_dat->data = data;
  temp_dat->data_d = NULL;
  temp_dat->name = dat->name;
  temp_dat->type = dat->type;
  temp_dat->size = dat->size;

  return temp_dat;
}

/*******************************************************************************
 * Routine to put (modify) a the data held in a distributed op_dat
 *******************************************************************************/

void op_mpi_put_data(op_dat dat)
{
  (void)dat;
  //the op_dat in parameter list is modified
  //need the orig_part_range and OP_part_list

  //need to do some checks to see if the input op_dat has the same dimensions
  //and other values as the internal op_dat
}

/*******************************************************************************
 * Debug/Diagnostics Routine to initialise import halo data to NaN
 *******************************************************************************/

static void op_reset_halo(op_arg* arg)
{
  op_dat dat = arg->dat;

  if((arg->opt) && (arg->argtype == OP_ARG_DAT) &&
    (arg->acc == OP_READ || arg->acc == OP_RW ) &&
    (dat->dirtybit == 1))
  {
    //printf("Resetting Halo of data array %10s\n",dat->name);
    halo_list imp_exec_list = OP_import_exec_list[dat->set->index];
    halo_list imp_nonexec_list = OP_import_nonexec_list[dat->set->index];

    // initialise import halo data to NaN
    int double_count = imp_exec_list->size*dat->size/sizeof(double);
    double_count +=  imp_nonexec_list->size*dat->size/sizeof(double);
    double* NaN = (double *)xmalloc(double_count* sizeof(double));
    for(int i = 0; i<double_count; i++) NaN[i] = (double)NAN;//0.0/0.0;

    int init = dat->set->size*dat->size;
    memcpy(&(dat->data[init]), NaN,
      dat->size*imp_exec_list->size + dat->size*imp_nonexec_list->size);
    free(NaN);
  }
}

void op_compute_moment(double t, double *first, double *second) {
  double times[2];
  double times_reduced[2];
  int comm_size;
  times[0] = t;
  times[1] = t*t;
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  MPI_Reduce(times, times_reduced, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

  *first = times_reduced[0]/(double)comm_size;
  *second = times_reduced[1]/(double)comm_size;
}

/*******************************************************************************
 * Routine to output performance measures
 *******************************************************************************/
void mpi_timing_output()
{
  int my_rank, comm_size;
  MPI_Comm OP_MPI_IO_WORLD;
  MPI_Comm_dup(MPI_COMM_WORLD, &OP_MPI_IO_WORLD);
  MPI_Comm_rank(OP_MPI_IO_WORLD, &my_rank);
  MPI_Comm_size(OP_MPI_IO_WORLD, &comm_size);

  unsigned int count, tot_count;
  count = HASH_COUNT(op_mpi_kernel_tab);
  MPI_Allreduce(&count,&tot_count , 1, MPI_INT, MPI_SUM, OP_MPI_IO_WORLD);

  if(tot_count > 0) {
    double tot_time;
    double avg_time;

    printf("___________________________________________________\n");
    printf("Performance information on rank %d\n", my_rank);
    printf("Kernel        Count  total time(sec)  Avg time(sec)  \n");

    op_mpi_kernel *k;
    for(k = op_mpi_kernel_tab; k != NULL; k=(op_mpi_kernel *)k->hh.next) {
      if (k->count > 0) {
        printf("%-10s  %6d       %10.4f      %10.4f    \n",
                k->name,k->count,  k->time,     k->time/k->count);

#ifdef COMM_PERF
        if(k->num_indices>0) {
          printf("halo exchanges:  ");
          for(int i = 0; i<k->num_indices; i++)
            printf("%10s ",k->comm_info[i]->name);
          printf("\n");
          printf("       count  :  ");
          for(int i = 0; i<k->num_indices; i++)
            printf("%10d ",k->comm_info[i]->count);printf("\n");
          printf("total(Kbytes) :  ");
          for(int i = 0; i< k->num_indices; i++)
            printf("%10d ",k->comm_info[i]->bytes/1024);printf("\n");
          printf("average(bytes):  ");
          for(int i = 0; i< k->num_indices; i++)
            printf("%10d ",k->comm_info[i]->bytes/k->comm_info[i]->count );printf("\n");
        }
        else
        {
          printf("halo exchanges:  %10s\n","NONE");
        }
        printf("---------------------------------------------------\n");
#endif
      }
    }
    printf("___________________________________________________\n");

    if(my_rank == MPI_ROOT) {
      printf("___________________________________________________\n");
      printf("\nKernel        Count   Max time(sec)   Avg time(sec)  \n");
    }

    for(k = op_mpi_kernel_tab; k != NULL; k=(op_mpi_kernel *)k->hh.next) {
      MPI_Reduce(&(k->count),&count, 1, MPI_INT, MPI_MAX, MPI_ROOT, OP_MPI_IO_WORLD);
      MPI_Reduce(&(k->time),&avg_time, 1, MPI_DOUBLE, MPI_SUM, MPI_ROOT, OP_MPI_IO_WORLD);
      MPI_Reduce(&(k->time),&tot_time, 1, MPI_DOUBLE, MPI_MAX, MPI_ROOT, OP_MPI_IO_WORLD);

      if(my_rank == MPI_ROOT && count > 0) {
        printf("%-10s  %6d       %10.4f      %10.4f    \n",
                k->name,count,   tot_time,   (avg_time)/comm_size);
      }
      tot_time = avg_time = 0.0;
    }
  }
  MPI_Comm_free(&OP_MPI_IO_WORLD);

}

/*******************************************************************************
 * Routine to measure timing for an op_par_loop / kernel
 *******************************************************************************/
void *op_mpi_perf_time(const char* name, double time)
{
  op_mpi_kernel *kernel_entry;

  HASH_FIND_STR(op_mpi_kernel_tab, name, kernel_entry);
  if (kernel_entry==NULL) {
    kernel_entry = (op_mpi_kernel *)xmalloc(sizeof(op_mpi_kernel));
    kernel_entry->num_indices = 0;
    kernel_entry->time = 0.0;
    kernel_entry->count = 0;
    strncpy ((char *)kernel_entry->name,name,NAMESIZE);
    HASH_ADD_STR( op_mpi_kernel_tab, name, kernel_entry );
  }

  kernel_entry->count += 1;
  kernel_entry->time += time;

  return (void *)kernel_entry;
}
#ifdef COMM_PERF

/*******************************************************************************
 * Routine to linear search comm_info array in an op_mpi_kernel for an op_dat
 *******************************************************************************/
int search_op_mpi_kernel(op_dat dat, op_mpi_kernel *kernal, int num_indices)
{
  for(int i = 0; i<num_indices; i++){
     if(strcmp((kernal->comm_info[i])->name, dat->name) == 0 &&
     (kernal->comm_info[i])->size == dat->size)
     {
       return i;
     }
  }

   return -1;
}



/*******************************************************************************
 * Routine to measure MPI message sizes exchanged in an op_par_loop / kernel
 *******************************************************************************/
void op_mpi_perf_comm(void *k_i, op_dat dat)
{
  halo_list exp_exec_list = OP_export_exec_list[dat->set->index];
  halo_list exp_nonexec_list = OP_export_nonexec_list[dat->set->index];
  int tot_halo_size = (exp_exec_list->size + exp_nonexec_list->size) * dat->size;

  op_mpi_kernel *kernel_entry = (op_mpi_kernel *)k_i;
  int num_indices = kernel_entry->num_indices;

  if(num_indices == 0)
  {
    //set capcity of comm_info array
    kernel_entry->cap = 20;
    op_dat_mpi_comm_info dat_comm = (op_dat_mpi_comm_info) xmalloc(sizeof(op_dat_mpi_comm_info_core));
    kernel_entry->comm_info = (op_dat_mpi_comm_info*)
    xmalloc(sizeof(op_dat_mpi_comm_info *)*(kernel_entry->cap));
    strncpy((char *)dat_comm->name,dat->name,20);
    dat_comm->size = dat->size;
    dat_comm->index = dat->index;
    dat_comm->count = 0;
    dat_comm->bytes = 0;

    //add first values
    dat_comm->count += 1;
    dat_comm->bytes += tot_halo_size;

    kernel_entry->comm_info[num_indices] = dat_comm;
    kernel_entry->num_indices++;
  }
  else
  {
    int index = search_op_mpi_kernel(dat, kernel_entry, num_indices);
    if(index < 0)
    {
      //increase capacity of comm_info array
      if(num_indices >= kernel_entry->cap)
      {
        kernel_entry->cap = kernel_entry->cap*2;
        kernel_entry->comm_info = (op_dat_mpi_comm_info*)
        xrealloc(kernel_entry->comm_info,
          sizeof(op_dat_mpi_comm_info *)*(kernel_entry->cap));
      }

      op_dat_mpi_comm_info dat_comm =
      (op_dat_mpi_comm_info) xmalloc(sizeof(op_dat_mpi_comm_info_core));

      strncpy((char *)dat_comm->name,dat->name,20);
      dat_comm->size = dat->size;
      dat_comm->index = dat->index;
      dat_comm->count = 0;
      dat_comm->bytes = 0;

      //add first values
      dat_comm->count += 1;
      dat_comm->bytes += tot_halo_size;

      kernel_entry->comm_info[num_indices] = dat_comm;
      kernel_entry->num_indices++;
    }
    else
    {
      kernel_entry->comm_info[index]->count += 1;
      kernel_entry->comm_info[index]->bytes += tot_halo_size;
    }
  }

}
#endif

#ifdef COMM_PERF
void op_mpi_perf_comms(void *k_i, int nargs, op_arg *args) {

  for (int n=0; n<nargs; n++) {
    if (args[n].argtype == OP_ARG_DAT && args[n].sent == 2)
    {
      op_mpi_perf_comm(k_i, (&args[n])->dat);
    }
  }
}
#endif


/*******************************************************************************
 * Routine to exit an op2 mpi application -
 *******************************************************************************/

void op_mpi_exit()
{
  //cleanup performance data - need to do this in some op_mpi_exit() routine
  op_mpi_kernel *kernel_entry, *tmp;
  HASH_ITER(hh, op_mpi_kernel_tab, kernel_entry, tmp) {
    HASH_DEL(op_mpi_kernel_tab, kernel_entry);
#ifdef COMM_PERF
    for(int i = 0; i<kernel_entry->num_indices; i++)
      free(kernel_entry->comm_info[i]);
#endif
    free(kernel_entry);
  }

  //free memory allocated to halos and mpi_buffers
  op_halo_destroy();
  //free memory used for holding partition information
  op_partition_destroy();
  //print each mpi process's timing info for each kernel
  for (int i = 0; i < OP_map_index; i++) {
    if (OP_map_partial_exchange && OP_map_partial_exchange[i]==0) continue;
    if (OP_import_nonexec_permap) {
      free(OP_import_nonexec_permap[i]->ranks);
      free(OP_import_nonexec_permap[i]->disps);
      free(OP_import_nonexec_permap[i]->sizes);
      free(OP_import_nonexec_permap[i]->list);
      free(OP_import_nonexec_permap[i]);
    }
    if (OP_export_nonexec_permap) {
      free(OP_export_nonexec_permap[i]->ranks);
      free(OP_export_nonexec_permap[i]->disps);
      free(OP_export_nonexec_permap[i]->sizes);
      free(OP_export_nonexec_permap[i]->list);
      free(OP_export_nonexec_permap[i]);
    }
  }
  free(set_import_buffer_size);
  free(OP_map_partial_exchange);
}

int getSetSizeFromOpArg (op_arg * arg)
{
  return arg->opt ? (arg->dat->set->size + OP_import_exec_list[arg->dat->set->index]->size +
                     OP_import_nonexec_list[arg->dat->set->index]->size) : 0;
}

int getHybridGPU() {
  return OP_hybrid_gpu;
}

int op_mpi_halo_exchanges(op_set set, int nargs, op_arg *args) {
  int size = set->size;
  int direct_flag = 1;

  if (OP_diags>0) {
    int dummy;
    for (int n=0; n<nargs; n++)
      op_arg_check(set,n,args[n],&dummy,"");
  }

  if (OP_hybrid_gpu) {
    for (int n=0; n<nargs; n++)
      if(args[n].opt && args[n].argtype == OP_ARG_DAT && args[n].dat->dirty_hd == 2) {
        op_download_dat(args[n].dat);
        args[n].dat->dirty_hd = 0;
      }
  }

  //check if this is a direct loop
  for (int n=0; n<nargs; n++)
    if(args[n].opt && args[n].argtype == OP_ARG_DAT && args[n].idx != -1)
      direct_flag = 0;

  if (direct_flag == 1) return size;

  //not a direct loop ...
  int exec_flag = 0;
  for (int n=0; n<nargs; n++) {
    if(args[n].opt && args[n].idx != -1 && args[n].acc != OP_READ) {
      size = set->size + set->exec_size;
      exec_flag = 1;
    }
  }
  op_timers_core(&c1, &t1);
  for (int n=0; n<nargs; n++) {
    if(args[n].opt && args[n].argtype == OP_ARG_DAT) {
      if (args[n].map == OP_ID) {
        op_exchange_halo(&args[n], exec_flag);
      } else {
        //Check if dat-map combination was already done or if there is a mismatch (same dat, diff map)
        int found = 0;
        int fallback = 0;
        for (int m = 0; m < nargs; m++) {
          if (m < n && args[n].dat == args[m].dat && args[n].map == args[m].map) found = 1;
          else if (args[n].dat == args[m].dat && args[n].map != args[m].map) fallback = 1;
        }
        //If there was a map mismatch with other argument, do full halo exchange
        if (fallback) op_exchange_halo(&args[n], exec_flag);
        else if (!found) { //Otherwise, if partial halo exchange is enabled for this map, do it
          if (OP_map_partial_exchange[args[n].map->index]) op_exchange_halo_partial(&args[n], exec_flag);
          else op_exchange_halo(&args[n], exec_flag);
        }
      }
    }
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
  return size;
}

int op_mpi_halo_exchanges_cuda(op_set set, int nargs, op_arg *args) {
  int size = set->size;
  int direct_flag = 1;

  if (OP_diags>0) {
    int dummy;
    for (int n=0; n<nargs; n++)
      op_arg_check(set,n,args[n],&dummy,"");
  }

  for (int n=0; n<nargs; n++)
    if(args[n].opt && args[n].argtype == OP_ARG_DAT && args[n].dat->dirty_hd == 1) {
      op_upload_dat(args[n].dat);
      args[n].dat->dirty_hd = 0;
    }

  //check if this is a direct loop
  for (int n=0; n<nargs; n++)
    if(args[n].opt && args[n].argtype == OP_ARG_DAT && args[n].idx != -1)
      direct_flag = 0;

  if (direct_flag == 1) return size;

  //not a direct loop ...
  int exec_flag = 0;
  for (int n=0; n<nargs; n++) {
    if(args[n].opt && args[n].idx != -1 && args[n].acc != OP_READ) {
      size = set->size + set->exec_size;
      exec_flag= 1;
    }
  }
  op_timers_core(&c1, &t1);
  for (int n=0; n<nargs; n++) {
    if(args[n].opt && args[n].argtype == OP_ARG_DAT) {
      if (args[n].map == OP_ID) {
        op_exchange_halo_cuda(&args[n], exec_flag);
      } else {
        //Check if dat-map combination was already done or if there is a mismatch (same dat, diff map)
        int found = 0;
        int fallback = 0;
        for (int m = 0; m < nargs; m++) {
          if (m < n && args[n].dat == args[m].dat && args[n].map == args[m].map) found = 1;
          else if (args[n].dat == args[m].dat && args[n].map != args[m].map) fallback = 1;
        }
        //If there was a map mismatch with other argument, do full halo exchange
        if (fallback) op_exchange_halo_cuda(&args[n], exec_flag);
        else if (!found) { //Otherwise, if partial halo exchange is enabled for this map, do it
          if (OP_map_partial_exchange[args[n].map->index]) op_exchange_halo_partial_cuda(&args[n], exec_flag);
          else op_exchange_halo_cuda(&args[n], exec_flag);
        }
      }
    }
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
  return size;
}

void op_mpi_set_dirtybit(int nargs, op_arg *args) {

  for (int n=0; n<nargs; n++) {
    if(args[n].argtype == OP_ARG_DAT)
    {
      set_dirtybit(&args[n],1);
    }
  }
}

void op_mpi_set_dirtybit_cuda(int nargs, op_arg *args) {

  for (int n=0; n<nargs; n++) {
    if(args[n].argtype == OP_ARG_DAT)
    {
      set_dirtybit(&args[n],2);
    }
  }
}

void op_mpi_wait_all(int nargs, op_arg *args) {
  op_timers_core(&c1, &t1);
  for (int n=0; n<nargs; n++) {
    op_wait_all(&args[n]);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

void op_mpi_wait_all_cuda(int nargs, op_arg *args) {
  op_timers_core(&c1, &t1);
  for (int n=0; n<nargs; n++) {
    op_wait_all_cuda(&args[n]);
  }
  op_timers_core(&c2, &t2);
  if (OP_kern_max>0) OP_kernels[OP_kern_curr].mpi_time += t2-t1;
}

void op_mpi_reset_halos(int nargs, op_arg *args) {
  for (int n=0; n<nargs; n++) {
    op_reset_halo(&args[n]);
  }
}

void op_mpi_barrier() {
  MPI_Barrier(MPI_COMM_WORLD);
}



/*******************************************************************************
 * Get the global size of a set
 *******************************************************************************/

int op_get_size(op_set set)
{
  int my_rank, comm_size;
  MPI_Comm_rank(OP_MPI_WORLD, &my_rank);
  MPI_Comm_size(OP_MPI_WORLD, &comm_size);
  int* sizes = (int *)malloc(sizeof(int)*comm_size);
  int g_size = 0;
  MPI_Allgather(&set->size, 1, MPI_INT, sizes, 1, MPI_INT, OP_MPI_WORLD);
  for(int i = 0; i<comm_size; i++)g_size = g_size + sizes[i];
  free(sizes);

  return g_size;
}
