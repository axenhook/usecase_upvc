#ifndef __RESULT_POOL_H__
#define __RESULT_POOL_H__

#include "dout.h"
#include "common.h"

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

#endif /* __RESULT_POOL_H__ */