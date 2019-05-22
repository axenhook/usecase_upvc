/**
 * @Copyright (c) 2016-2019 - Dominique Lavenier & UPMEM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>

#include "mram_dpu.h"
#include "dpus_mgmt.h"
#include "upvc_dpu.h"
#include "genome.h"
#include "index.h"
#include "dispatch.h"
#include "upvc.h"
#include "vmi.h"
#include "backends_functions.h"
#include "common.h"
#include "parse_args.h"

static void dispatch_request_add(dispatch_request_t *reads,
                                 unsigned int offset,
                                 unsigned int count,
                                 unsigned int num,
                                 int8_t *nbr,
                                 reads_info_t *reads_info)
{
        dpu_request_t *new_read =
                (dpu_request_t *) (reads->reads_area + reads->nb_reads * DPU_REQUEST_SIZE(reads_info->size_neighbour_in_bytes));
        new_read->offset = offset;
        new_read->count = count;
        new_read->num = num;

        memcpy(((uint8_t *)new_read) + sizeof(dpu_request_t), nbr, (size_t) reads_info->size_neighbour_in_bytes);
        reads->nb_reads++;
}

void add_seed_to_dpu_requests(dispatch_request_t *requests,
                              int num_read,
                              __attribute__((unused)) int nb_read_written,
                              index_seed_t *seed,
                              int8_t *nbr,
                              reads_info_t *reads_info)
{
        dispatch_request_t *this_request = requests + seed->num_dpu;
        dispatch_request_add(this_request,
                             (unsigned int) seed->offset,
                             (unsigned int) seed->nb_nbr,
                             (unsigned int) num_read,
                             nbr,
                             reads_info);
}

void run_on_dpu(dispatch_request_t *dispatch,
                devices_t *devices,
                unsigned int dpu_offset,
                unsigned int rank_id,
                unsigned int round,
                unsigned int nb_pass,
                sem_t *dispatch_free_sem,
                sem_t *acc_wait_sem,
                times_ctx_t *times_ctx,
                reads_info_t *reads_info)
{
        double t1, t2, t3, t4;
        unsigned int nb_dpus_per_rank = devices->nb_dpus_per_rank;

        t1 = my_clock();

        if ((DEBUG_DPU == -1) || ((DEBUG_DPU / nb_dpus_per_rank) == (rank_id + dpu_offset / nb_dpus_per_rank))) {

                PRINT_TIME_WRITE_READS(times_ctx, nb_pass, rank_id);
                dpu_try_write_dispatch_into_mram(rank_id,
                                                 dpu_offset + rank_id * nb_dpus_per_rank,
                                                 devices,
                                                 dispatch,
                                                 reads_info);
                sem_post(dispatch_free_sem);

                PRINT_TIME_WRITE_READS(times_ctx, nb_pass, rank_id);
                t2 = my_clock();
                PRINT_TIME_COMPUTE(times_ctx, nb_pass, rank_id);

                dpu_try_run(rank_id, devices);
                while (!dpu_try_check_status(rank_id, devices));

                PRINT_TIME_COMPUTE(times_ctx, nb_pass, rank_id);
                t3 = my_clock();
                PRINT_TIME_READ_RES(times_ctx, nb_pass, rank_id);

                if (DEBUG_DPU == -1) {
                        sem_wait(acc_wait_sem);
                }
                /* Gather results and free DPUs */
                dpu_result_out_t *results[nb_dpus_per_rank];
                for (unsigned int each_dpu = 0; each_dpu < nb_dpus_per_rank; each_dpu++) {
                        unsigned int this_dpu = rank_id * nb_dpus_per_rank + dpu_offset + each_dpu;
                        results[each_dpu] = get_mem_dpu_res(this_dpu);
                }
                dpu_try_get_results_and_log(round, nb_pass, rank_id, dpu_offset, devices, results);

                PRINT_TIME_READ_RES(times_ctx, nb_pass, rank_id);

        } else {
                sem_post(dispatch_free_sem);
		t2 = my_clock();
		t3 = my_clock();
        }

        t4 = my_clock();
        times_ctx->map_read = t4 - t1;
        times_ctx->tot_map_read += t4 - t1;
        times_ctx->write_reads = t2 - t1;
        times_ctx->tot_write_reads += t2 - t1;
        times_ctx->compute = t3 - t2;
        times_ctx->tot_compute += t3 - t2;
        times_ctx->read_result = t4 - t3;
        times_ctx->tot_read_result += t4 - t3;
}

void init_backend_dpu(unsigned int *nb_rank,
                      devices_t **devices,
                      unsigned int nb_dpu_per_run,
                      const char *dpu_binary,
                      index_seed_t ***index_seed,
                      __attribute__((unused)) reads_info_t *reads_info)
{
        *index_seed = load_index_seeds();
        malloc_dpu_res(get_nb_dpu());
        *devices = dpu_try_alloc_for(nb_dpu_per_run, dpu_binary);
        *nb_rank = (*devices)->nb_ranks_per_run;
}

void free_backend_dpu(devices_t *devices, unsigned int nb_dpu)
{
        dpu_try_free(devices);
        free_dpu_res(nb_dpu);
}

void load_mram_dpu(unsigned int dpu_offset, unsigned int rank_id, devices_t *devices, reads_info_t *reads_info, times_ctx_t *times_ctx)
{
        double t1, t2;
        unsigned int nb_dpus_per_rank = devices->nb_dpus_per_rank;
        mram_info_t **mram = (mram_info_t **)malloc(nb_dpus_per_rank * sizeof(mram_info_t *));
        assert(mram != NULL);

        t1 = my_clock();

        if ((DEBUG_DPU == -1) || ((DEBUG_DPU / nb_dpus_per_rank) == (rank_id + dpu_offset / nb_dpus_per_rank))) {
                for (unsigned int each_dpu = 0; each_dpu < nb_dpus_per_rank; each_dpu++) {
                        mram[each_dpu] = (mram_info_t *)malloc(MRAM_SIZE);
                        assert(mram[each_dpu] != NULL);
                }

                for (unsigned int each_dpu = 0; each_dpu < nb_dpus_per_rank; each_dpu++) {
                        unsigned int this_dpu = dpu_offset + each_dpu + rank_id * nb_dpus_per_rank;
                        mram_load(mram[each_dpu], this_dpu);
                        mram[each_dpu]->delta = reads_info->delta_neighbour_in_bytes;
                }
                dpu_try_write_mram(rank_id, devices, mram);

                for (unsigned int each_dpu = 0; each_dpu < nb_dpus_per_rank; each_dpu++) {
                        free(mram[each_dpu]);
                }
                free(mram);
        }

        t2 = my_clock();
        times_ctx->write_mram = t2 - t1;
        times_ctx->tot_write_mram += t2 - t1;
}
