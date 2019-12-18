/**
 * Copyright 2016-2019 - Dominique Lavenier & UPMEM
 */

#include <alloc.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <string.h>

#include "debug.h"
#include "dout.h"
#include "stats.h"

#include "common.h"

/**
 * @brief Common structure to write back results.
 *
 * This output FIFO is shared by the tasklets to write back results to host, thus is protected by a critical section.
 *
 * @var cache      Local cache to perform memory transfers.
 * @var mutex      Critical section that protects the pool.
 * @var wridx      Index of the current output in the FIFO.
 * @var cur_write  Where to write in MRAM.
 */
typedef struct {
    uint8_t cache[LOCAL_RESULTS_PAGE_SIZE];
    mutex_id_t mutex;
    unsigned int wridx;
    mram_addr_t cur_write;
} result_pool_t;

/**
 * @brief The result pool shared by tasklets.
 */
__dma_aligned static result_pool_t result_pool;
MUTEX_INIT(result_pool_mutex);

/**
 * @brief The buffer of result in mram.
 */
__mram_noinit dpu_result_out_t DPU_RESULT_VAR[MAX_DPU_RESULTS];

__mram_noinit uint64_t DPU_RESULTS_CHECKSUM_VAR;
uint64_t checksum;

static uint64_t compute_checksum(uint64_t *buffer, size_t size_in_bytes)
{
    uint64_t res = 0ULL;
    for (unsigned int id = 0; id < (size_in_bytes / sizeof(uint64_t)); id++) {
        res += buffer[id];
    }
    return res;
}

#define DPU_RESULT_WRITE(res, addr)                                                                                              \
    do {                                                                                                                         \
        mram_write16(res, addr);                                                                                                 \
    } while (0)
_Static_assert(sizeof(dpu_result_out_t) == 16, "dpu_result_out_t size changed (make sure that DPU_RESULT_WRITE changed as well)");

void result_pool_init()
{
    result_pool.mutex = MUTEX_GET(result_pool_mutex);
    result_pool.wridx = 0;
    result_pool.cur_write = (mram_addr_t)DPU_RESULT_VAR;
    checksum = 0ULL;
}

void result_pool_write(const dout_t *results, STATS_ATTRIBUTE dpu_tasklet_stats_t *stats)
{
    unsigned int each_result = 0;
    unsigned int pageno;

    mutex_lock(result_pool.mutex);

    /* Read back and write the swapped results */
    for (pageno = 0; pageno < results->nb_page_out; pageno++) {
        mram_addr_t source_addr = dout_swap_page_addr(results, pageno);

        if (result_pool.wridx + MAX_LOCAL_RESULTS_PER_READ >= (MAX_DPU_RESULTS - 1)) {
            printf("WARNING! too many result in DPU! (from swap)\n");
            halt();
        }

        ASSERT_DMA_ADDR(source_addr, result_pool.cache, LOCAL_RESULTS_PAGE_SIZE);
        STATS_INCR_LOAD(stats, LOCAL_RESULTS_PAGE_SIZE);
        LOCAL_RESULTS_PAGE_READ(source_addr, result_pool.cache);

        checksum += compute_checksum((uint64_t *)result_pool.cache, LOCAL_RESULTS_PAGE_SIZE);

        ASSERT_DMA_ADDR(result_pool.cur_write, result_pool.cache, LOCAL_RESULTS_PAGE_SIZE);
        STATS_INCR_STORE(stats, LOCAL_RESULTS_PAGE_SIZE);
        STATS_INCR_STORE_RESULT(stats, LOCAL_RESULTS_PAGE_SIZE);
        LOCAL_RESULTS_PAGE_WRITE(result_pool.cache, result_pool.cur_write);

        result_pool.wridx += MAX_LOCAL_RESULTS_PER_READ;
        result_pool.cur_write += LOCAL_RESULTS_PAGE_SIZE;
    }

    while ((each_result < results->nb_cached_out) && (result_pool.wridx < (MAX_DPU_RESULTS - 1))) {
        /* Ensure that the size of a result out structure is two longs. */
        ASSERT_DMA_ADDR(result_pool.cur_write, &(results->outs[each_result]), sizeof(dpu_result_out_t));
        STATS_INCR_STORE(stats, sizeof(dpu_result_out_t));
        STATS_INCR_STORE_RESULT(stats, sizeof(dpu_result_out_t));
        DPU_RESULT_WRITE((void *)&(results->outs[each_result]), result_pool.cur_write);

        checksum += compute_checksum((uint64_t *)&(results->outs[each_result]), sizeof(dpu_result_out_t));

        result_pool.wridx++;
        result_pool.cur_write += sizeof(dpu_result_out_t);
        each_result++;
    }
    if (result_pool.wridx >= (MAX_DPU_RESULTS - 1)) {
        printf("WARNING! too many result in DPU! (from local)\n");
        halt();
    }

    mutex_unlock(result_pool.mutex);
}

void result_pool_finish(STATS_ATTRIBUTE dpu_tasklet_stats_t *stats)
{
    __dma_aligned static const dpu_result_out_t end_of_results
        = { .num = (unsigned int)-1, .score = (unsigned int)-1, .coord.seq_nr = 0, .coord.seed_nr = 0 };
    /* Note: will fill in the result pool until MAX_DPU_RESULTS -1, to be sure that the very last result
     * has a num equal to -1.
     */
    mutex_lock(result_pool.mutex);
    /* Mark the end of result data, do not increment the indexes, so that the next one restarts from this
     * point.
     */
    DPU_RESULT_WRITE((void *)&end_of_results, result_pool.cur_write);
    STATS_INCR_STORE(stats, sizeof(dpu_result_out_t));
    STATS_INCR_STORE_RESULT(stats, sizeof(dpu_result_out_t));

    DPU_RESULTS_CHECKSUM_VAR = checksum;

    mutex_unlock(result_pool.mutex);
}
