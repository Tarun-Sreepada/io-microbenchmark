#include "async.h"
#include "config.h"



// Asynchronous I/O operation using io_uring
void io_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
 
}

// Time-based benchmarking using io_uring
void time_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{

    params.io = params.duration * 1e6; // estimate number of I/O operations
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Create a new io_uring instance
    struct io_uring ring;

    // Initialize io_uring instance
    if (io_uring_queue_init(params.queue_depth, &ring, 0) < 0)
    {
        std::cerr << "Error: io_uring initialization failed\n";
        exit(1);
    }

    char **buffers = new char *[params.queue_depth];
    bool *is_buffer_free = new bool[params.queue_depth];

    for (int i = 0; i < params.queue_depth; i++)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size) != 0)
        {
            throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
        }
        is_buffer_free[i] = true;

    }

    uint32_t submitted = 0;

    struct io_uring_cqe *cqes[params.queue_depth];

    stats.start_time = get_current_time_ns();

    while(true)
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

            uint32_t buffer_id = acquire_buffer(is_buffer_free, params.queue_depth);
            if (buffer_id == -1)
            {
                std::cerr << "Error: No free buffers available\n";  // This should never happen
            }

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, params.fd, buffers[buffer_id], params.page_size, offsets[submitted % params.io]);
            }
            else
            {
                io_uring_prep_read(sqe, params.fd, buffers[buffer_id], params.page_size, offsets[submitted % params.io]);
            }

            // in user_data, store the buffer_id and the request_id 32bit + 32bit = 64bit aka user_data is 64bit
            sqe->user_data = combine32To64(buffer_id, submitted % params.io);
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
            auto [buffer_id, request_id] = extractBoth32(req_id);

            if (cqe->res < 0)
            {
                // Handle error
                std::cerr << "I/O error on request " << req_id << ": " << strerror(-cqe->res) << "\n";
            }
            else if (cqe->res != params.page_size)
            {
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                // Resubmit incomplete I/O
                if (sqe)
                {
                    // Adjust remaining bytes and buffer pointer
                    uint64_t bytes_done = cqe->res;
                    uint64_t remaining_bytes = params.page_size - bytes_done;

                    if (params.read_or_write == "write")
                    {
                        io_uring_prep_write(sqe, params.fd,
                                            buffers[buffer_id] + bytes_done,
                                            remaining_bytes,
                                            offsets[request_id] + bytes_done);
                    }
                    else
                    { // read
                        io_uring_prep_read(sqe, params.fd,
                                           buffers[buffer_id] + bytes_done,
                                           remaining_bytes,
                                           offsets[request_id] + bytes_done);
                    }

                    sqe->user_data = req_id; // Maintain user_data for tracking
                }
                else
                {
                    std::cerr << "Failed to get SQE for resubmission\n";
                }
            }
            else
            {
                // Successful completion
                is_buffer_free[buffer_id] = true;
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

    
    delete[] buffers;
    delete[] is_buffer_free;


}