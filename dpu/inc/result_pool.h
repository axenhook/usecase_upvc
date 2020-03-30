/**
 * Copyright 2016-2019 - Dominique Lavenier & UPMEM
 */

#ifndef __RESULT_POOL_H__
#define __RESULT_POOL_H__

#include "common.h"
#include "dout.h"

#include <mram.h>

__mram_ptr void *get_result_addr();
size_t get_result_size();

/**
 * @brief Pushes a bunch of results.
 *
 * @param results  The list of outputs.
 * @param stats    To update statistical report.
 */
void result_pool_write(const dout_t *results, dpu_tasklet_stats_t *stats);

/**
 * @brief Initializes the result pool.
 */
void result_pool_init();

/**
 * @brief Add end mark in result pool.
 */
void result_pool_finish(dpu_tasklet_stats_t *stats);

#endif /* __RESULT_POOL_H__ */
