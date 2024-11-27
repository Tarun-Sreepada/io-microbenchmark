#include "iou.h"
#include "config.h"
#include <condition_variable>

std::queue<struct io_uring_cqe *> cqe_queue;
std::mutex queue_mutex;
std::condition_variable cv;
std::atomic<bool> stop_reaping(false);


void reap_cqes_async(submitter *s, uint64_t &completed_ios) {
    struct app_io_cq_ring *cring = &s->cq_ring;
    unsigned head;

    while (!stop_reaping) {
        head = *cring->head;
        unsigned tail = *cring->tail;

        while (head != tail) {
            read_barrier();
            struct io_uring_cqe *cqe = &cring->cqes[head & *cring->ring_mask];

            {
                // Push CQE to the queue for async processing
                std::lock_guard<std::mutex> lock(queue_mutex);
                cqe_queue.push(cqe);
            }
            cv.notify_one();

            head++;
            completed_ios++;
        }

        *cring->head = head;
        write_barrier();

        // Avoid busy waiting
        // std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
}

void process_cqes(size_t page_size) {
    while (!stop_reaping) {
        struct io_uring_cqe *cqe = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [] { return !cqe_queue.empty() || stop_reaping; });

            if (stop_reaping && cqe_queue.empty()) {
                break;
            }

            cqe = cqe_queue.front();
            cqe_queue.pop();
        }

        if (cqe) {
            struct io_data *io = (struct io_data *)cqe->user_data;

            if (cqe->res < 0) {
                std::cerr << "I/O error: " << strerror(-cqe->res) << std::endl;
            } else if ((size_t)cqe->res != io->length) {
                std::cerr << "Partial I/O: " << cqe->res << " bytes" << std::endl;
            }

            if ((size_t)cqe->res != page_size) {
                std::cerr << "Error: Completed I/O size (" << cqe->res
                          << " bytes) does not match page size (" << page_size
                          << " bytes)" << std::endl;
            }

            free(io->buf);
            delete io;
        }
    }
}

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

void reap_cqes(submitter *s, uint64_t ret, uint64_t &completed_ios)
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

    params.io = params.duration * 1e6; // estimate number of I/O operations
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    struct submitter *s = new submitter();

    if (app_setup_uring(s, params.queue_depth))
    {
        throw std::runtime_error("Error setting up io_uring");
    }

    uint64_t submitted, to_submit = 0;
    submitted = 0;

    stats.start_time = get_current_time_ns();

    while(true)
    {
        if (stats.io_completed >= params.io)
        {
            break;
        }

        while ((submitted - stats.io_completed) < params.queue_depth)
        {
            struct io_data *io = new io_data();
            submit_io(s, params.fd, params.page_size, offsets[submitted % params.io], params.read_or_write == "read", io);
            submitted++;
            to_submit++;
        }

        int ret = io_uring_enter(s->ring_fd, to_submit, 1, IORING_ENTER_GETEVENTS, NULL);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_enter failed: " + std::string(strerror(-ret)));
        }
        to_submit = 0;

        reap_cqes(s, ret, stats.io_completed);
    }

    stats.end_time = get_current_time_ns();

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

    params.io = params.duration * 1e6; // estimate number of I/O operations
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    struct submitter *s = new submitter();

    if (app_setup_uring(s, params.queue_depth))
    {
        throw std::runtime_error("Error setting up io_uring");
    }

    uint64_t submitted, to_submit = 0;
    submitted = 0;

    stats.start_time = get_current_time_ns();

    std::thread reaper_thread(reap_cqes_async, s, std::ref(stats.io_completed));
    std::thread processing_thread(process_cqes, params.page_size);

    while(true)
    {
        uint64_t current_time = get_current_time_ns();
        if (current_time - stats.start_time >= params.duration * 1e9)
        {
            break;
        }

        while ((submitted - stats.io_completed) < params.queue_depth)
        {
            struct io_data *io = new io_data();
            submit_io(s, params.fd, params.page_size, offsets[submitted % params.io], params.read_or_write == "read", io);
            submitted++;
            to_submit++;
        }

        int ret = io_uring_enter(s->ring_fd, to_submit, 0, IORING_ENTER_GETEVENTS, NULL);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_enter failed: " + std::string(strerror(-ret)));
        }
        to_submit = 0;

        // reap_cqes(s, ret, stats.io_completed);
    }

    stats.end_time = get_current_time_ns();

    stop_reaping = true;
    cv.notify_all();
    reaper_thread.join();
    processing_thread.join();

    munmap(s->sq_ptr, s->sring_sz);
    if (s->cq_ptr && s->cq_ptr != s->sq_ptr)
    {
        munmap(s->cq_ptr, s->cring_sz);
    }
    munmap(s->sqes, s->sqes_sz);
    close(s->ring_fd);

    delete s;


}