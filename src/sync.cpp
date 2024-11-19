#include "sync.h"
#include "config.h"

// Function to handle write operation
bool handle_write(int fd, char *buffer, uint64_t page_size, uint64_t offset)
{
    ssize_t bytes_written = pwrite(fd, buffer, page_size, offset);
    return bytes_written == static_cast<ssize_t>(page_size);
}

// Function to handle read operation
bool handle_read(int fd, char *buffer, uint64_t page_size, uint64_t offset)
{
    ssize_t bytes_read = pread(fd, buffer, page_size, offset);
    return bytes_read == static_cast<ssize_t>(page_size);
}

void io_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    auto operation = (params.read_or_write == "write") ? handle_write : handle_read;

    // Generate offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // allocate buffer
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    double submission_time = 0;
    double completion_time = 0;
    stats.latencies.resize(params.io, 0);
    int max_retries = 5; // Maximum number of retries

    auto start_time = std::chrono::high_resolution_clock::now();

    // if its sequential, use preadv/pwritev but if its random, use pread/pwrite
    // since we are using synchronous I/O, we can use pread/pwrite and cap the queue depth to 1
    for (uint64_t i = 0; i < params.io; ++i)
    {
        bool success = false;                    // Flag to indicate if the operation succeeded
        int attempt = 0;                         // Attempt counter
        submission_time = get_current_time_ns(); // Record the first submission time

        while (!success && attempt < max_retries)
        {
            try
            {
                success = operation(params.fd, buffer, params.page_size, offsets[i]); // Call the operation
                if (success)
                    break;
            }
            catch (const std::runtime_error &e)
            {
                ++attempt;
                if (attempt < max_retries)
                {
                    std::cerr << "Attempt " << attempt << " failed. Retrying...\n";
                }
                else
                {
                    std::cerr << "Max retries reached. Aborting operation.\n";
                    throw; // Re-throw the exception after max retries
                }
            }
        }

        // Record the completion time after successful operation
        completion_time = get_current_time_ns();

        // Calculate latency
        stats.latencies[i] = completion_time - submission_time;
        stats.io_completed++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration<double>(end_time - start_time).count();

    // convert latency to microseconds
    for (uint64_t i = 0; i < params.io; ++i)
    {
        stats.latencies[i] /= 1000;
    }

    // Free buffer
    free(buffer);
}

void time_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Select the operation based on read or write
    auto operation = (params.read_or_write == "write") ? handle_write : handle_read;

    // Generate initial offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate a buffer aligned to the page size
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    // Measure preads that can be done in 1 second for time refresh
    uint64_t loops = 0;
    uint64_t start_time = get_current_time_ns();
    while (get_current_time_ns() - start_time < 1e9)
    {
        int ret = operation(params.fd, buffer, params.page_size, offsets[loops % params.io]);
        loops++;
    }
    uint64_t end_time = get_current_time_ns();

    // Adjust the number of operations based on measured performance
    params.io = loops * params.duration * 2;       // Double it to accommodate fluctuations
    offsets = generate_offsets(params, thread_id); // Regenerate offsets

    stats.latencies.resize(params.io, 0); // Initialize latencies vector

    // Start the timed benchmark
    start_time = get_current_time_ns();
    uint64_t next_check_time = start_time + params.refresh_interval;
    params.refresh_interval = 8e8; // Update refresh interval to 200ms

    std::ostringstream stats_buffer;                 // Buffer to accumulate stats output

    while (true)
    {
        uint64_t current_time = get_current_time_ns();

        // Perform the operation
        int ret = operation(params.fd, buffer, params.page_size, offsets[stats.io_completed % params.io]);
        if (ret < 0)
        {
            std::cerr << "Error during operation: " << strerror(errno) << "\n";
            break;
        }
        uint64_t completion_time = get_current_time_ns();

        stats.io_completed++;
        stats.latencies[stats.io_completed % params.io] = completion_time - current_time;

        // Check the elapsed time lazily
        if (completion_time >= next_check_time)
        {
            // Calculate elapsed time, IOPS, and throughput
            double elapsed_time = (completion_time - start_time) / 1e9;
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

            // Break condition for elapsed time
            if ((completion_time - start_time) > (params.duration * 1e9))
            {
                std::cout << std::endl; // End line on completion
                break;
            }
            next_check_time += params.refresh_interval; // Update next check time
        }
    }
    // Calculate total time taken
    end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;


    // Convert latencies to microseconds for better granularity
    for (uint64_t i = 0; i < params.io; ++i)
    {
        stats.latencies[i] /= 1000;
    }

    // Free the allocated buffer
    free(buffer);
}
