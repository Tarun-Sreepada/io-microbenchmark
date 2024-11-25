#pragma once
#include "config.h"
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <linux/io_uring.h>

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



// Works in VSCode when highlighting a function by hovering over it :)
/**
 * @brief Performs the io_uring_setup system call.
 *
 * @param entries Number of submission queue entries.
 * @param p Pointer to io_uring_params structure.
 * @return File descriptor on success, -1 on failure.
 */
int io_uring_setup(unsigned entries, struct io_uring_params *p);

/**
 * @brief Performs the io_uring_enter system call.
 *
 * @param ring_fd File descriptor of the io_uring instance.
 * @param to_submit Number of submissions to submit.
 * @param min_complete Minimum number of completions.
 * @param flags Flags for the system call.
 * @return Number of events submitted on success, -1 on failure.
 */
int io_uring_enter(int ring_fd, unsigned int to_submit,
                   unsigned int min_complete, unsigned int flags);


void io_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id);

void time_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id);