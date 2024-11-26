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

        if (params.read_or_write == "write")
        {
            while (ret < params.page_size)
            {
                ret += pwrite(params.fd, buffer + ret, params.page_size - ret, offsets[0] + ret);
            }
        }
        else
        {
            while (ret < params.page_size)
            {
                ret += pread(params.fd, buffer + ret, params.page_size - ret, offsets[0] + ret);
            }
        }

        stats.io_completed++;
        stats.latencies[stats.io_completed - 1] = get_current_time_ns() - current_time;
        ret = 0;
    }
    stats.end_time = get_current_time_ns();


    // Free the allocated buffer
    free(buffer);
}
