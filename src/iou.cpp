#include "iou.h"
#include "config.h"
#include <condition_variable>


int app_setup_uring(struct submitter *s, int queue_depth)
{
    struct app_io_sq_ring *sring = &s->sq_ring;
    struct app_io_cq_ring *cring = &s->cq_ring;
    struct io_uring_params p;
    void *sq_ptr, *cq_ptr;

    memset(&p, 0, sizeof(p));
    s->ring_fd = io_uring_setup(queue_depth, &p);
    if (s->ring_fd < 0) {
        perror("io_uring_setup");
        return 1;
    }

    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        if (cring_sz > sring_sz) {
            sring_sz = cring_sz;
        }
        cring_sz = sring_sz;
    }

    sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE,
                  s->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE,
                      s->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }

    /* Correct pointer calculations */
    sring->head = (unsigned *)((char *)sq_ptr + p.sq_off.head);
    sring->tail = (unsigned *)((char *)sq_ptr + p.sq_off.tail);
    sring->ring_mask = (unsigned *)((char *)sq_ptr + p.sq_off.ring_mask);
    sring->ring_entries = (unsigned *)((char *)sq_ptr + p.sq_off.ring_entries);
    sring->flags = (unsigned *)((char *)sq_ptr + p.sq_off.flags);
    sring->array = (unsigned *)((char *)sq_ptr + p.sq_off.array);

    s->sqes = (struct io_uring_sqe *)mmap(0, p.sq_entries * sizeof(struct io_uring_sqe),
                                          PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                          s->ring_fd, IORING_OFF_SQES);

    if (s->sqes == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    cring->head = (unsigned *)((char *)cq_ptr + p.cq_off.head);
    cring->tail = (unsigned *)((char *)cq_ptr + p.cq_off.tail);
    cring->ring_mask = (unsigned *)((char *)cq_ptr + p.cq_off.ring_mask);
    cring->ring_entries = (unsigned *)((char *)cq_ptr + p.cq_off.ring_entries);
    cring->cqes = (struct io_uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);

    return 0;
}
void submit_io(struct submitter *s, int fd, size_t block_size, off_t offset, bool is_read, struct io_data *io, char *buffer, int buffer_id, int request_id)
{
    struct app_io_sq_ring *sring = &s->sq_ring;
    unsigned tail, index;

    tail = *sring->tail;
    index = tail & *sring->ring_mask;

    struct io_uring_sqe *sqe = &s->sqes[index];
    memset(sqe, 0, sizeof(*sqe));

    io->length = block_size;
    io->offset = offset;
    io->buf = buffer;
    io->buffer_id = buffer_id;
    io->request_id = request_id;

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
    }

    sring->array[index] = index;
    tail++;
    *sring->tail = tail;
    write_barrier();
}

void reap_cqes(struct submitter *s, uint64_t &completed_ios, bool *is_buffer_free)
{
    struct app_io_cq_ring *cring = &s->cq_ring;
    unsigned head = *cring->head;

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

        // Mark the buffer as free for reuse
        if (io->buffer_id >= 0) {  // Ensure buffer_id is valid
            is_buffer_free[io->buffer_id] = true;
        }
        else {
            std::cerr << "Invalid buffer_id: " << io->buffer_id << std::endl;
        }

        head++;
        completed_ios++;
    }

    *cring->head = head;
    write_barrier();
}

void io_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{

}



void time_benchmark_thread_iou(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{

    pin_thread(thread_id);
    std::cout << "Pin thread " << thread_id << std::endl;


    params.io = params.duration * 1e6; // estimate number of I/O operations
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    struct submitter *s = new submitter();

    if (app_setup_uring(s, params.queue_depth))
    {
        throw std::runtime_error("Error setting up io_uring");
    }

    char **buffers = new char *[params.queue_depth];
    bool *is_buffer_free = new bool[params.queue_depth];
    struct io_data **io_data_pool = new struct io_data *[params.queue_depth];
    for (int i = 0; i < params.queue_depth; i++)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size))
        {
            throw std::runtime_error("posix_memalign failed");
        }
        is_buffer_free[i] = true;
        io_data_pool[i] = new io_data(); // Preallocate io_data structures
    }

    uint64_t submitted = 0, to_submit = 0;

    stats.start_time = get_current_time_ns();

    while (true)
    {
        uint64_t current_time = get_current_time_ns();
        if (current_time - stats.start_time >= params.duration * 1e9)
        {
            break;
        }

        while ((submitted - stats.io_completed) < params.queue_depth)
        {
            uint32_t buffer_id = acquire_buffer(is_buffer_free, params.queue_depth);
            if (buffer_id == -1)
            {
                break;
            }

            struct io_data *io = io_data_pool[buffer_id]; // Reuse preallocated io_data
            submit_io(s, params.fd, params.page_size, offsets[submitted], params.read_or_write == "read", io, buffers[buffer_id], buffer_id, submitted);
            to_submit++;
            submitted++;
        }

        int ret = io_uring_enter(s->ring_fd, to_submit, 1, IORING_ENTER_GETEVENTS, NULL);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_enter failed: " + std::string(strerror(-ret)));
        }
        to_submit = 0;

        reap_cqes(s, stats.io_completed, is_buffer_free);
    }

    stats.end_time = get_current_time_ns();

    for (int i = 0; i < params.queue_depth; i++)
    {
        free(buffers[i]);
        delete io_data_pool[i];
    }
    delete[] buffers;
    delete[] is_buffer_free;
    delete[] io_data_pool;

    munmap(s->sq_ptr, s->sring_sz);
    if (s->cq_ptr && s->cq_ptr != s->sq_ptr)
    {
        munmap(s->cq_ptr, s->cring_sz);
    }
    munmap(s->sqes, s->sqes_sz);
    close(s->ring_fd);


    delete s;
}