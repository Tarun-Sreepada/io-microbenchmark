#include "sync.h"
#include "config.h"

void io_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Generate offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);
    stats.latencies.resize(params.io, 0);

    // allocate buffer
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    int ret = 0;
    stats.start_time = get_current_time_ns();

    for (uint64_t i = 0; i < params.io; ++i)
    {
        uint64_t current_time = get_current_time_ns();
        if (params.read_or_write == "write")
        {
            while (ret < params.page_size)
            {
                ret += pwrite(params.fd, buffer + ret, params.page_size - ret, offsets[i] + ret);
            }
        }
        else
        {
            while (ret < params.page_size)
            {
                ret += pread(params.fd, buffer + ret, params.page_size - ret, offsets[i] + ret);
            }
        }

        stats.io_completed++;
        stats.latencies[i] = get_current_time_ns() - current_time;
        ret = 0;
    }

    stats.end_time = get_current_time_ns();

    // Free the allocated buffer
    free(buffer);
}

void time_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{

    pin_thread(thread_id);

    params.io = 1e6 * params.duration; // 1M I/O operations per second theoretically
    stats.latencies.resize(params.io, 0);

    // Generate initial offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate a buffer aligned to the page size
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    int ret = 0;
    stats.start_time = get_current_time_ns();

    while (true)
    {
        uint64_t current_time = get_current_time_ns();
        if (current_time - stats.start_time > 1e9 * params.duration)
        {
            break;
        }

        while (ret < params.page_size)
        {
            int bytes = (params.read_or_write == "write")
                            ? pwrite(params.fd, buffer + ret, params.page_size - ret, offsets[stats.io_completed % params.io] + ret)
                            : pread(params.fd, buffer + ret, params.page_size - ret, offsets[stats.io_completed % params.io] + ret);

            if (bytes == -1)
            {
                // Log the error and continue
                std::cerr << "Thread " << thread_id << " encountered an error: "
                          << strerror(errno)
                          << " at offset " << offsets[stats.io_completed % params.io]
                          << "\n";

                // Reset ret and proceed to the next operation
                ret = 0;
                break; // Exit the inner loop and try the next I/O operation
            }

            ret += bytes;
        }

        // Record successful I/O or attempt regardless of errors
        stats.io_completed++;

        // Log latency only for successful I/O
        if (ret > 0)
        {
            stats.latencies[stats.io_completed - 1] = get_current_time_ns() - current_time;
        }

        ret = 0; // Reset for the next iteration
    }

    stats.end_time = get_current_time_ns();

    // Free the allocated buffer
    free(buffer);
}
