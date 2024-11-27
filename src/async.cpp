#include "async.h"
#include "config.h"

// Asynchronous I/O operation using io_uring
void io_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Initialize io_uring
    struct io_uring ring;

    if (io_uring_queue_init(params.queue_depth, &ring, 0) < 0)
    {
        throw std::runtime_error("io_uring_queue_init failed");
    }

    // Generate initial offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate buffer
    char **buffers = (char **)malloc(params.queue_depth * sizeof(char *));
    for (int i = 0; i < params.queue_depth; i++)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size) != 0)
        {
            throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
        }
    }

    uint64_t submitted = 0;

    stats.start_time = get_current_time_ns();

    while (stats.io_completed != params.io)
    {
        while (submitted - stats.io_completed < params.queue_depth)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                break; // No more SQEs available
            }

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, params.fd, buffers[submitted % params.queue_depth], params.page_size, offsets[submitted % params.io]);
            }
            else
            { // read
                io_uring_prep_read(sqe, params.fd, buffers[submitted % params.queue_depth], params.page_size, offsets[submitted % params.io]);
            }

            sqe->user_data = submitted; // Track request by its index
            submitted++;
        }

        // Submit all queued requests to the kernel
        int ret = io_uring_submit(&ring);

        if (ret < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-ret)));
        }

        // Retrieve completions
        struct io_uring_cqe *cqe;

        int count = io_uring_peek_batch_cqe(&ring, &cqe, params.queue_depth);
        for (int i = 0; i < count; i++)
        {

            if (cqe->res != params.page_size)
            {
                // Resubmit incomplete I/O
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (sqe)
                {
                    uint64_t req_id = cqe->user_data;
                    if (params.read_or_write == "write")
                    {
                        io_uring_prep_write(sqe, params.fd, buffers[req_id % params.queue_depth] + cqe->res, params.page_size - cqe->res, offsets[req_id]);
                    }
                    else
                    { // read
                        io_uring_prep_read(sqe, params.fd, buffers[req_id % params.queue_depth] + cqe->res, params.page_size - cqe->res, offsets[req_id]);
                    }
                    sqe->user_data = req_id; // Maintain user_data for tracking
                    submitted++;
                }
                else
                {
                    std::cerr << "Failed to get SQE for resubmission\n";
                }
            }
            else
            {
                // Successful completion
                stats.io_completed++;
            }

            // Mark the CQE as seen
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    stats.end_time = get_current_time_ns();

    // Free resources
    io_uring_queue_exit(&ring);

    for (int i = 0; i < params.queue_depth; i++)
    {
        free(buffers[i]);
    }

    free(buffers);
}

void time_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Initialize io_uring
    struct io_uring ring;

    if (io_uring_queue_init(params.queue_depth, &ring, 0) < 0)
    {
        throw std::runtime_error("io_uring_queue_init failed");
    }

    // Generate initial offsets
    params.io = params.duration * 1e6; // estimate number of I/O operations
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate buffer
    char **buffers = (char **)malloc(params.queue_depth * sizeof(char *));
    for (int i = 0; i < params.queue_depth; i++)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size) != 0)
        {
            throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
        }
    }

    uint64_t submitted = 0;
    struct io_uring_cqe *cqes[params.queue_depth];

    stats.start_time = get_current_time_ns();

    while (true)
    {
        uint64_t current_time = get_current_time_ns();
        if (current_time - stats.start_time >= params.duration * 1e9)
        {
            break;
        }

        while (submitted - stats.io_completed < params.queue_depth)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                break; // No more SQEs available
            }

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, params.fd,
                                    buffers[submitted % params.queue_depth],
                                    params.page_size,
                                    offsets[submitted % params.io]);
            }
            else
            { // read
                io_uring_prep_read(sqe, params.fd,
                                   buffers[submitted % params.queue_depth],
                                   params.page_size,
                                   offsets[submitted % params.io]);
            }

            sqe->user_data = submitted; // Track request by its index
            submitted++;
        }

        // Submit all queued requests to the kernel
        int ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-ret)));
        }

        // Retrieve completions
        int count = io_uring_peek_batch_cqe(&ring, cqes, params.queue_depth);

        for (int i = 0; i < count; i++)
        {
            struct io_uring_cqe *cqe = cqes[i];
            uint64_t req_id = cqe->user_data; // Retrieve original request ID

            if (cqe->res < 0)
            {
                // Handle error
                std::cerr << "I/O error on request " << req_id << ": " << strerror(-cqe->res) << "\n";
            }
            else if (cqe->res != params.page_size)
            {
                // Resubmit incomplete I/O
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (sqe)
                {
                    // Adjust remaining bytes and buffer pointer
                    uint64_t bytes_done = cqe->res;
                    uint64_t remaining_bytes = params.page_size - bytes_done;

                    if (params.read_or_write == "write")
                    {
                        io_uring_prep_write(sqe, params.fd,
                                            buffers[req_id % params.queue_depth] + bytes_done,
                                            remaining_bytes,
                                            offsets[req_id] + bytes_done);
                    }
                    else
                    { // read
                        io_uring_prep_read(sqe, params.fd,
                                           buffers[req_id % params.queue_depth] + bytes_done,
                                           remaining_bytes,
                                           offsets[req_id] + bytes_done);
                    }

                    sqe->user_data = req_id; // Reuse the same user_data
                }
                else
                {
                    std::cerr << "Failed to get SQE for resubmission\n";
                }
            }
            else
            {
                // Successful completion
                stats.io_completed++;
            }

            // Mark the CQE as seen
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    stats.end_time = get_current_time_ns();

    // Free resources
    io_uring_queue_exit(&ring);

    for (int i = 0; i < params.queue_depth; i++)
    {
        free(buffers[i]);
    }

    free(buffers);
}
