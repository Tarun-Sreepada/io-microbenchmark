#pragma once
#include "config.h"
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <liburing.h>

/* Memory barriers */
#define read_barrier() __asm__ __volatile__("" ::: "memory")
#define write_barrier() __asm__ __volatile__("" ::: "memory")

struct app_io_sq_ring
{
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    unsigned *flags;
    unsigned *array;
};

struct app_io_cq_ring
{
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    struct io_uring_cqe *cqes;
};

struct submitter
{
    int ring_fd;
    void *sq_ptr;
    void *cq_ptr;
    size_t sring_sz;
    size_t cring_sz;
    size_t sqes_sz;
    struct app_io_sq_ring sq_ring;
    struct io_uring_sqe *sqes;
    struct app_io_cq_ring cq_ring;
};

struct io_data
{
    void *buf;
    off_t offset;
    size_t length;
};



/**
 * @brief IO benchmark thread for io_uring engine.
 * Executes number of IO operations specified in the benchmark parameters and measures the time taken.
 * 
 * @param params Benchmark parameters.
 * @param stats Thread statistics.
 * @param thread_id Thread ID.
 */
void io_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id);


/**
 * @brief Time-based benchmark thread for io_uring engine.
 * Executes IO operations for a specified duration and measures the number of successful operations.
 * 
 * @param params Benchmark parameters.
 * @param stats Thread statistics.
 * @param thread_id Thread ID.
 */
void time_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id);