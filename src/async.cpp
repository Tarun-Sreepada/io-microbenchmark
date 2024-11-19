#include "sync.h"
#include "config.h"
#include <liburing.h>


// Asynchronous I/O operation using io_uring
void io_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Initialize io_uring
    struct io_uring ring;
    int queue_depth = params.queue_depth;
    if (io_uring_queue_init(queue_depth, &ring, 0) < 0)
    {
        throw std::runtime_error("io_uring_queue_init failed");
    }

    // Generate offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate buffer
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size * queue_depth) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    // Initialize submission and completion time tracking
    stats.latencies.resize(params.io, 0);

    // Start time
    auto start_time = std::chrono::high_resolution_clock::now();

    // Prepare and submit I/O requests
    uint64_t io_remaining = params.io;
    uint64_t io_submitted = 0;
    uint64_t io_completed = 0;

    while (io_completed < params.io)
    {
        // Submit as many requests as possible up to queue depth
        while (io_submitted - io_completed < (uint64_t)queue_depth && io_submitted < params.io)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                break;
            }

            uint64_t offset = offsets[io_submitted];
            void *buf = buffer + ((io_submitted % queue_depth) * params.page_size);
            int fd = params.fd;

            // Record submission time
            uint64_t submission_time = get_current_time_ns();

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, fd, buf, params.page_size, offset);
            }
            else // read
            {
                io_uring_prep_read(sqe, fd, buf, params.page_size, offset);
            }

            // Store submission time in user_data
            sqe->user_data = submission_time;

            io_submitted++;
        }

        // Submit the batch of requests
        int ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-ret)));
        }

        // Wait for completions
        struct io_uring_cqe *cqe;
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_wait_cqe failed: " + std::string(strerror(-ret)));
        }

        // Process completions
        while (io_completed < io_submitted && (ret = io_uring_peek_cqe(&ring, &cqe)) == 0 && cqe)
        {
            // Get completion time
            uint64_t completion_time = get_current_time_ns();

            // Get submission time from user_data
            uint64_t submission_time = cqe->user_data;

            // Calculate latency
            stats.latencies[io_completed] = completion_time - submission_time;

            if (cqe->res < 0)
            {
                std::cerr << "I/O operation failed: " << strerror(-cqe->res) << "\n";
            }
            else if ((uint64_t)cqe->res != params.page_size)
            {
                std::cerr << "Incomplete I/O operation: expected " << params.page_size << " bytes, got " << cqe->res << "\n";
            }

            // Mark completion
            io_completed++;

            // Mark cqe as seen
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    // End time
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration<double>(end_time - start_time).count();

    // Convert latencies to microseconds
    for (uint64_t i = 0; i < params.io; ++i)
    {
        stats.latencies[i] /= 1000;
    }

    // Free buffer and io_uring resources
    free(buffer);
    io_uring_queue_exit(&ring);
}

void time_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Initialize io_uring
    struct io_uring ring;
    int queue_depth = params.queue_depth;
    if (io_uring_queue_init(queue_depth, &ring, 0) < 0)
    {
        throw std::runtime_error("io_uring_queue_init failed");
    }

    // Generate initial offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate buffer
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size * queue_depth) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    // Start time
    uint64_t start_time = get_current_time_ns();
    params.refresh_interval = 5e8; // 5 seconds
    uint64_t next_check_time = start_time + params.refresh_interval;
    uint64_t total_duration_ns = params.duration * 1e9; // duration in nanoseconds

    uint64_t io_submitted = 0;
    uint64_t io_completed = 0;
    uint64_t total_io = 0;

    stats.io_completed = 0;
    std::ostringstream stats_buffer;

    struct io_uring_cqe *cqes[params.queue_depth];

    while (true)
    {
        uint64_t current_time = get_current_time_ns();

        // Check if total duration has elapsed
        if (current_time - start_time >= total_duration_ns)
        {
            break;
        }

        // Submit as many requests as possible up to queue depth
        while (io_submitted - io_completed < (uint64_t)queue_depth)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                break;
            }

            uint64_t offset = offsets[io_submitted % offsets.size()];
            void *buf = buffer + ((io_submitted % queue_depth) * params.page_size);
            int fd = params.fd;

            // Record submission time
            uint64_t submission_time = get_current_time_ns();

            if (params.read_or_write == "write")
            {
                io_uring_prep_write(sqe, fd, buf, params.page_size, offset);
            }
            else // read
            {
                io_uring_prep_read(sqe, fd, buf, params.page_size, offset);
            }

            // Store submission time in user_data
            sqe->user_data = submission_time;

            io_submitted++;
        }

        // Submit the batch of requests
        int ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-ret)));
        }

        unsigned int num_cqes = io_uring_peek_batch_cqe(&ring, cqes, params.queue_depth);
        for (unsigned int i = 0; i < num_cqes; ++i)
        {
            struct io_uring_cqe *cqe = cqes[i];

            if (cqe->res < 0)
            {
                std::cerr << "I/O operation failed: " << strerror(-cqe->res) << "\n";
            }
            else if ((uint64_t)cqe->res != params.page_size)
            {
                std::cerr << "Incomplete I/O operation: expected " << params.page_size << " bytes, got " << cqe->res << "\n";
            }

            // Get submission time from user_data
            uint64_t submission_time = cqe->user_data;

            // Calculate latency
            stats.latencies.push_back(get_current_time_ns() - submission_time);

            // Mark completion
            io_completed++;
            stats.io_completed++;

            // Mark cqe as seen
            io_uring_cqe_seen(&ring, cqe);
        }


        // Periodically output stats
        current_time = get_current_time_ns();
        if (current_time >= next_check_time)
        {
            double elapsed_time = (current_time - start_time) / 1e9;
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

            next_check_time += params.refresh_interval;
        }
    }

    // Finish processing any remaining completions
    while (io_completed < io_submitted)
    {
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_wait_cqe failed: " + std::string(strerror(-ret)));
        }

        // Get completion time
        uint64_t completion_time = get_current_time_ns();

        // Get submission time from user_data
        uint64_t submission_time = cqe->user_data;

        // Calculate latency
        stats.latencies.push_back(completion_time - submission_time);

        if (cqe->res < 0)
        {
            std::cerr << "I/O operation failed: " << strerror(-cqe->res) << "\n";
        }
        else if ((uint64_t)cqe->res != params.page_size)
        {
            std::cerr << "Incomplete I/O operation: expected " << params.page_size << " bytes, got " << cqe->res << "\n";
        }

        // Mark completion
        io_completed++;
        stats.io_completed++;

        // Mark cqe as seen
        io_uring_cqe_seen(&ring, cqe);
    }

    // Calculate total time taken
    uint64_t end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;

    // Convert latencies to microseconds
    for (uint64_t i = 0; i < stats.latencies.size(); ++i)
    {
        stats.latencies[i] /= 1000;
    }

    // Clear the stats line
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;

    // Free resources
    free(buffer);
    io_uring_queue_exit(&ring);
}
