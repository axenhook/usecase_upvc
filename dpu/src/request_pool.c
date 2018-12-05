#include <mram.h>
#include <mutex.h>
#include <alloc.h>
#include <string.h>
#include <defs.h>

#include "mutex_def.h"
#include "debug.h"
#include "request_pool.h"
#include "dout.h"
#include "stats.h"

#include "common.h"

/**
 * @brief Common structure to consume requests.
 *
 * Requests belong to a FIFO, from which each tasklet picks the reads. The FIFO is protected by a critical
 * section.
 *
 * @var mutex         Critical section that protects the pool.
 * @var nb_reads      The number of reads in the request pool.
 * @var rdidx         Index of the first unread read in the request pool.
 * @var cur_read      Address of the first read to be processed in MRAM.
 * @var request_size  Size of a request and the neighbour corresponding.
 */
typedef struct {
        mutex_t mutex;
        unsigned int nb_reads;
        unsigned int rdidx;
        mram_addr_t cur_read;
        unsigned int request_size;
} request_pool_t;

/**
 * @brief Common request pool, shared by every tasklet.
 */
static request_pool_t request_pool;

void request_pool_init(mram_info_t *mram_info)
{
        __attribute__((aligned(8))) request_info_t io_data;

        request_pool.mutex = mutex_get(MUTEX_REQUEST_POOL);

        REQUEST_INFO_READ(DPU_REQUEST_INFO_ADDR(mram_info), &io_data);
        request_pool.nb_reads = io_data.nb_reads;
        request_pool.rdidx = 0;
        request_pool.cur_read = (mram_addr_t) DPU_REQUEST_ADDR(mram_info);
        request_pool.request_size = DPU_REQUEST_SIZE(mram_info->nbr_len);
        DEBUG_REQUESTS_PRINT_POOL(request_pool);
}

bool request_pool_next(uint8_t *request_buffer, STATS_ATTRIBUTE dpu_tasklet_stats_t *stats)
{
        mutex_lock(request_pool.mutex);
        if (request_pool.rdidx == request_pool.nb_reads) {
                mutex_unlock(request_pool.mutex);
                return false;
        }

        /* Fetch next request into cache */
        ASSERT_DMA_ADDR(request_pool.cur_read, request_buffer, request_pool.request_size);
        ASSERT_DMA_LEN(request_pool.request_size);
        STATS_INCR_LOAD(stats, request_pool.request_size);
        STATS_INCR_LOAD_DATA(stats, request_pool.request_size);
        mram_readX(request_pool.cur_read, (void *) request_buffer, request_pool.request_size);

        /* Point to next request */
        request_pool.rdidx++;
        request_pool.cur_read += request_pool.request_size;
        mutex_unlock(request_pool.mutex);

        return true;
}