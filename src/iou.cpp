#include "iou.h"
#include "config.h"



/**
 * @brief Performs the io_uring_setup system call.
 *
 * @param entries Number of submission queue entries.
 * @param p Pointer to io_uring_params structure.
 * @return File descriptor on success, -1 on failure.
 */
int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

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
                   unsigned int min_complete, unsigned int flags)
{
    return (int)syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                        flags, NULL, 0);
}



/**
 * @brief Sets up the io_uring instance and maps the rings.
 *
 * @param s Pointer to the submitter structure.
 * @param queue_depth Depth of the submission queue.
 * @return 0 on success, 1 on failure.
 */
int app_setup_uring(struct submitter *s, unsigned queue_depth)
{
    struct app_io_sq_ring *sring = &s->sq_ring;
    struct app_io_cq_ring *cring = &s->cq_ring;
    struct io_uring_params p;
    void *sq_ptr, *cq_ptr;

    memset(&p, 0, sizeof(p));
    s->ring_fd = io_uring_setup(queue_depth, &p);
    if (s->ring_fd < 0)
    {
        perror("io_uring_setup");
        return 1;
    }

    s->sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    s->cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    if (p.features & IORING_FEAT_SINGLE_MMAP)
    {
        if (s->cring_sz > s->sring_sz)
        {
            s->sring_sz = s->cring_sz;
        }
        s->cring_sz = s->sring_sz;
    }

    sq_ptr = mmap(0, s->sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE,
                  s->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED)
    {
        perror("mmap");
        return 1;
    }
    s->sq_ptr = sq_ptr;

    if (p.features & IORING_FEAT_SINGLE_MMAP)
    {
        cq_ptr = sq_ptr;
    }
    else
    {
        cq_ptr = mmap(0, s->cring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE,
                      s->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED)
        {
            perror("mmap");
            munmap(sq_ptr, s->sring_sz);
            return 1;
        }
        s->cq_ptr = cq_ptr;
    }

    /* Correct pointer calculations */
    sring->head = (unsigned *)((char *)sq_ptr + p.sq_off.head);
    sring->tail = (unsigned *)((char *)sq_ptr + p.sq_off.tail);
    sring->ring_mask = (unsigned *)((char *)sq_ptr + p.sq_off.ring_mask);
    sring->ring_entries = (unsigned *)((char *)sq_ptr + p.sq_off.ring_entries);
    sring->flags = (unsigned *)((char *)sq_ptr + p.sq_off.flags);
    sring->array = (unsigned *)((char *)sq_ptr + p.sq_off.array);

    s->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
    s->sqes = (struct io_uring_sqe *)mmap(0, s->sqes_sz,
                                          PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                          s->ring_fd, IORING_OFF_SQES);

    if (s->sqes == MAP_FAILED)
    {
        perror("mmap");
        munmap(s->sq_ptr, s->sring_sz);
        if (!(p.features & IORING_FEAT_SINGLE_MMAP))
        {
            munmap(s->cq_ptr, s->cring_sz);
        }
        return 1;
    }

    cring->head = (unsigned *)((char *)cq_ptr + p.cq_off.head);
    cring->tail = (unsigned *)((char *)cq_ptr + p.cq_off.tail);
    cring->ring_mask = (unsigned *)((char *)cq_ptr + p.cq_off.ring_mask);
    cring->ring_entries = (unsigned *)((char *)cq_ptr + p.cq_off.ring_entries);
    cring->cqes = (struct io_uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);

    return 0;
}

/**
 * @brief Submits an I/O operation using io_uring.
 *
 * @param s Pointer to the submitter structure.
 * @param fd File descriptor of the device.
 * @param block_size Size of each I/O operation.
 * @param offset Offset for the I/O operation.
 * @param is_read True for read, false for write.
 * @param io Pointer to the io_data structure.
 */
void submit_io(struct submitter *s, int fd, size_t block_size, off_t offset, bool is_read, struct io_data *io)
{
    struct app_io_sq_ring *sring = &s->sq_ring;
    unsigned tail, index;

    tail = *sring->tail;
    index = tail & *sring->ring_mask;

    struct io_uring_sqe *sqe = &s->sqes[index];
    memset(sqe, 0, sizeof(*sqe));

    io->length = block_size;
    io->offset = offset;
    if (posix_memalign(&io->buf, 4096, block_size))
    { // Align to 4096 bytes
        perror("posix_memalign");
        exit(1);
    }

    sqe->fd = fd;
    sqe->addr = (unsigned long)io->buf;
    sqe->len = block_size;
    sqe->off = offset;
    sqe->user_data = (unsigned long long)io;

    if (is_read)
    {
        sqe->opcode = IORING_OP_READ;
    }
    else
    {
        sqe->opcode = IORING_OP_WRITE;
        memset(io->buf, 0xAA, block_size); // Fill buffer with dummy data
    }

    sring->array[index] = index;
    tail++;
    *sring->tail = tail;
    write_barrier();
}

/**
 * @brief Reaps completed I/O operations from the completion queue.
 *
 * @param s Pointer to the submitter structure.
 * @param completed_ios Atomic counter for completed I/Os.
 * @param total_bytes Atomic counter for total bytes transferred.
 */
void reap_cqes(submitter *s, uint64_t &completed_ios)
{
    struct app_io_cq_ring *cring = &s->cq_ring;
    unsigned head;

    head = *cring->head;

    while (head != *cring->tail)
    {
        read_barrier();
        struct io_uring_cqe *cqe = &cring->cqes[head & *cring->ring_mask];
        struct io_data *io = (struct io_data *)cqe->user_data;

        if (cqe->res < 0)
        {
            std::cerr << "I/O error: " << strerror(-cqe->res) << std::endl;
        }
        else if ((size_t)cqe->res != io->length)
        {
            std::cerr << "Partial I/O: " << cqe->res << " bytes" << std::endl;
        }

        free(io->buf);
        delete io;

        head++;
        completed_ios++;
    }

    *cring->head = head;
    write_barrier();
}







void io_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    struct submitter *s = new submitter();
    memset(s, 0, sizeof(submitter));

    if (app_setup_uring(s, params.queue_depth))
    {
        std::cerr << "Failed to set up io_uring\n";
        exit(EXIT_FAILURE);
    }

    params.device_size = get_device_size(params.fd);
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    uint64_t submitted_ios = 0;
    uint64_t to_submit = 0;

    uint64_t start_time = get_current_time_ns();

    for (uint64_t i = 0; i < params.io; ++i)
    {
        while(submitted_ios - stats.io_completed >= params.queue_depth)
        {
            submit_io(s, params.fd, params.page_size, offsets[i], params.read_or_write == "read", new io_data());
            submitted_ios++;
            to_submit++;
        }
        int ret = io_uring_enter(s->ring_fd, to_submit, 0, 0);
        if (ret < 0)
        {
            perror("io_uring_enter");
            exit(EXIT_FAILURE);
        }
        to_submit = 0;
        reap_cqes(s, stats.io_completed);
    }

    while (stats.io_completed < params.io)
    {
        int ret = io_uring_enter(s->ring_fd, 0, params.queue_depth, 0);
        if (ret < 0)
        {
            perror("io_uring_enter");
            exit(EXIT_FAILURE);
        }
        reap_cqes(s, stats.io_completed);
    }

    uint64_t end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;

    delete s;

    munmap(s->sq_ptr, s->sring_sz);
    if (s->cq_ptr && s->cq_ptr != s->sq_ptr)
    {
        munmap(s->cq_ptr, s->cring_sz);
    }
    munmap(s->sqes, s->sqes_sz);
    close(s->ring_fd);
    delete s;

}

void time_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    struct submitter *s = new submitter();
    memset(s, 0, sizeof(submitter));

    if (app_setup_uring(s, params.queue_depth))
    {
        std::cerr << "Failed to set up io_uring\n";
        exit(EXIT_FAILURE);
        delete s;
    }

    params.device_size = get_device_size(params.fd);
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    uint64_t submitted_ios = 0;
    uint64_t to_submit = 0;

    uint64_t start_time = get_current_time_ns();


    while (true)
    {
        uint64_t now = get_current_time_ns();
        if (now - start_time > 1e9 * params.duration)
        {
            break;
        }

        while(submitted_ios - stats.io_completed < params.queue_depth)
        {
            submit_io(s, params.fd, params.page_size, offsets[submitted_ios], params.read_or_write == "read", new io_data());
            submitted_ios++;
            to_submit++;
        }
        int ret = io_uring_enter(s->ring_fd, to_submit, 1, 0);
        if (ret < 0)
        {
            perror("io_uring_enter");
            exit(EXIT_FAILURE);
        }
        to_submit = 0;
        reap_cqes(s, stats.io_completed);

    }

    uint64_t end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;

    reap_cqes(s, stats.io_completed);

    delete s;

    munmap(s->sq_ptr, s->sring_sz);
    if (s->cq_ptr && s->cq_ptr != s->sq_ptr)
    {
        munmap(s->cq_ptr, s->cring_sz);
    }

    munmap(s->sqes, s->sqes_sz);
    close(s->ring_fd);
    // delete s;


}