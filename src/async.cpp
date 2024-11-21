#include "sync.h"
#include "config.h"
#include <liburing.h>

void async_refresh_interval_acquire(benchmark_params &params, char *buffer, std::vector<uint64_t> &offsets, uint64_t &loops, bool &loop, uint64_t start_time, uint64_t granularity)
{
    // Initialize io_uring
    struct io_uring ring;
    int ret = io_uring_queue_init(params.queue_depth, &ring, 0);
    if (ret < 0)
    {
        std::cerr << "Error initializing io_uring: " << strerror(-ret) << "\n";
        return;
    }

    uint64_t completed_loops = 0;

    while (loop)
    {
        // Submit I/O requests
        for (unsigned int i = 0; i < params.queue_depth && loops < offsets.size(); ++i, ++loops)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                std::cerr << "Error getting submission queue entry\n";
                break;
            }

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, params.fd, buffer, params.page_size, offsets[loops]);
            }
            else
            {
                io_uring_prep_read(sqe, params.fd, buffer, params.page_size, offsets[loops]);
            }

            sqe->user_data = loops; // Associate a loop ID with this I/O request
        }

        ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            std::cerr << "Error submitting io_uring requests: " << strerror(-ret) << "\n";
            break;
        }

        // Process completions
        for (unsigned int i = 0; i < params.queue_depth && completed_loops < loops; ++i)
        {
            struct io_uring_cqe *cqe;
            ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0)
            {
                std::cerr << "Error waiting for completion: " << strerror(-ret) << "\n";
                break;
            }

            if (cqe->res < 0)
            {
                std::cerr << "I/O operation failed: " << strerror(-cqe->res) << "\n";
            }
            else
            {
                // I/O completed successfully
                ++completed_loops;
            }

            io_uring_cqe_seen(&ring, cqe);
        }

        // Check for loop exit condition
        if (loops % granularity == 0 && get_current_time_ns() - start_time > 1e9)
        {
            loop = false;
        }
    }

    // Cleanup
    io_uring_queue_exit(&ring);
}

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
        if (params.read_or_write == "write")
        {
            __builtin_prefetch(buffers[i], 1, 3);
        }
        else
        {
            __builtin_prefetch(buffers[i], 0, 3);
        }
    }

    // Measure number of operations that can be done in 1 second for time refresh
    uint64_t loops = 0;
    uint64_t start_time = get_current_time_ns();
    bool loop = true;
    async_refresh_interval_acquire(params, buffers[0], offsets, loops, loop, start_time, 1000); // measure every 1000 operations for time
    uint64_t end_time = get_current_time_ns();

    uint64_t increment = loops;
    uint64_t submitted = 0;
    std::ostringstream stats_buffer;

    struct io_uring_cqe *cqes[params.queue_depth];

    stats.latencies.resize(params.io, 0);
    offsets = generate_offsets(params, thread_id);

    // Start time
    loop = true;
    start_time = get_current_time_ns();
    while (loop)
    {
        // Submit new I/O requests if the queue has space
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
        unsigned int count = io_uring_peek_batch_cqe(&ring, cqes, params.queue_depth);

        for (unsigned int i = 0; i < count; ++i)
        {
            struct io_uring_cqe *cqe = cqes[i];

            if (cqe->res != params.page_size)
            {
                // Resubmit incomplete I/O
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (sqe)
                {
                    uint64_t req_id = cqe->user_data;
                    if (params.read_or_write == "write")
                    {
                        io_uring_prep_write(sqe, params.fd, buffers[req_id % params.queue_depth], params.page_size, offsets[req_id]);
                    }
                    else
                    { // read
                        io_uring_prep_read(sqe, params.fd, buffers[req_id % params.queue_depth], params.page_size, offsets[req_id]);
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
                if (stats.io_completed == params.io)
                {
                    loop = false;
                    break;
                }
            }

            // Mark the CQE as seen
            io_uring_cqe_seen(&ring, cqe);
        }

        if (stats.io_completed >= loops)
        {
            loops += increment;
            uint64_t current_time = get_current_time_ns();
            double elapsed_time = (current_time - start_time);

            elapsed_time /= 1e9;

            double iops = stats.io_completed / elapsed_time;
            double throughput = (stats.io_completed * params.page_size) / elapsed_time;

            // Buffer stats to avoid frequent terminal writes
            stats_buffer.str("");
            stats_buffer << "\rThread " << thread_id
                            << ": IO/S = " << std::fixed << std::setprecision(2) << iops
                            << ", Throughput = " << throughput / 1e6 << " MB/s"
                            << ", Elapsed Time = " << std::fixed << std::setprecision(2)
                            << (elapsed_time) << "s";
            std::cout << stats_buffer.str() << std::flush;

        }
    }

    end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;

    // mark all cqe as seen
    for (unsigned int i = 0; i < params.queue_depth; i++)
    {
        io_uring_cqe_seen(&ring, cqes[i]);
    }

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
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate buffer
    char **buffers = (char **)malloc(params.queue_depth * sizeof(char *));
    for (int i = 0; i < params.queue_depth; i++)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size) != 0)
        {
            throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
        }
        if (params.read_or_write == "write")
        {
            __builtin_prefetch(buffers[i], 1, 3);
        }
        else
        {
            __builtin_prefetch(buffers[i], 0, 3);
        }
    }

    // Measure number of operations that can be done in 1 second for time refresh
    uint64_t loops = 0;
    uint64_t start_time = get_current_time_ns();
    bool loop = true;
    async_refresh_interval_acquire(params, buffers[0], offsets, loops, loop, start_time, 1000); // measure every 1000 operations for time
    uint64_t end_time = get_current_time_ns();

    uint64_t increment = loops;
    uint64_t submitted = 0;
    std::ostringstream stats_buffer;

    struct io_uring_cqe *cqes[params.queue_depth];

    params.io = loops * params.duration * 2; // Double it to accommodate fluctuations
    stats.latencies.resize(params.io, 0);
    offsets = generate_offsets(params, thread_id);

    // Start time
    loop = true;
    start_time = get_current_time_ns();
    while (loop)
    {
        // Submit new I/O requests if the queue has space
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
        // std::cout << "Submitting " << submitted << " requests\n";
        int ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-ret)));
        }

        // Retrieve completions
        unsigned int count = io_uring_peek_batch_cqe(&ring, cqes, params.queue_depth);
        // std::cout << "Received " << count << " completions\n";

        for (unsigned int i = 0; i < count; ++i)
        {
            struct io_uring_cqe *cqe = cqes[i];

            if (cqe->res != params.page_size)
            {
                // Resubmit incomplete I/O
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (sqe)
                {
                    uint64_t req_id = cqe->user_data;
                    if (params.read_or_write == "write")
                    {
                        io_uring_prep_write(sqe, params.fd, buffers[req_id % params.queue_depth], params.page_size, offsets[req_id]);
                    }
                    else
                    { // read
                        io_uring_prep_read(sqe, params.fd, buffers[req_id % params.queue_depth], params.page_size, offsets[req_id]);
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


        // std::cout << "Completed " << stats.io_completed << " operations\n";
        if (stats.io_completed >= loops)
        {
            // std::cout << "Incrementing loops\n";
            loops += increment;
            uint64_t current_time = get_current_time_ns();
            double elapsed_time = (current_time - start_time);

            if (elapsed_time > 1e9 * params.duration)
            {
                loop = false;
                break;
            }

            elapsed_time /= 1e9;

            double iops = stats.io_completed / elapsed_time;
            double throughput = (stats.io_completed * params.page_size) / elapsed_time;

            // Buffer stats to avoid frequent terminal writes
            stats_buffer.str("");
            stats_buffer << "\rThread " << thread_id
                            << ": IOPS = " << std::fixed << std::setprecision(2) << iops
                            << ", Throughput = " << throughput / 1e6 << " MB/s"
                            << ", Remaining Time = " << std::fixed << std::setprecision(2)
                            << (params.duration - elapsed_time) << "s";
            std::cout << stats_buffer.str() << std::flush;

        }
    }

    end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;

    // mark all cqe as seen
    for (unsigned int i = 0; i < params.queue_depth; i++)
    {
        io_uring_cqe_seen(&ring, cqes[i]);
    }

    // Free resources
    io_uring_queue_exit(&ring);

    for (int i = 0; i < params.queue_depth; i++)
    {
        free(buffers[i]);
    }

    free(buffers);
}
