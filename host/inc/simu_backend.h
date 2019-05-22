/**
 * @Copyright (c) 2016-2019 - Dominique Lavenier & UPMEM
 */

#ifndef __SIMU_BACKEND_H__
#define __SIMU_BACKEND_H__

#include <semaphore.h>

#include "genome.h"
#include "vmi.h"
#include "upvc.h"
#include "index.h"
#include "dispatch.h"
#include "backends_functions.h"

/**
 * @brief Write seed information in the structure representing the DPU list of requests.
 */
void add_seed_to_simulation_requests(dispatch_request_t *requests,
                                     int num_read,
                                     int nb_read_written,
                                     index_seed_t *seed,
                                     int8_t *nbr,
                                     reads_info_t *reads_info);

/**
 * @brief Compute one pass in simulation mode.
 */
void run_dpu_simulation(dispatch_request_t *dispatch,
                        devices_t *devices,
                        unsigned int dpu_offset,
                        unsigned int rank_id,
                        unsigned int round,
                        unsigned int nb_pass,
                        sem_t *dispatch_free_sem,
                        sem_t *acc_wait_sem,
                        times_ctx_t *times_ctx,
                        reads_info_t *reads_info);

/**
 * @brief Index the reference genome.
 */
void init_backend_simulation(unsigned int *nb_rank,
                             devices_t **devices,
                             unsigned int nb_dpu_per_run,
                             const char *dpu_binary,
                             index_seed_t ***index_seed,
                             reads_info_t *reads_info);

/**
 * @brief Free structure used by simulation.
 */
void free_backend_simulation(devices_t *devices, unsigned int nb_dpu);

/**
 * @brief Load mram in structure that will represente the DPUs.
 */
void load_mram_simulation(unsigned int dpu_offset,
                          unsigned int rank_id,
                          devices_t *devices,
                          reads_info_t *reads_info,
                          times_ctx_t *times_ctx);

#endif /* __SIMU_BACKEND_H__ */
