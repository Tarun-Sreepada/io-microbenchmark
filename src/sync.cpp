#include "sync.h"
#include "config.h"

void sync_refresh_interval_acquire(benchmark_params &params, char *buffer, std::vector<uint64_t> &offsets, uint64_t &loops, bool &loop, uint64_t start_time, uint64_t granularity)
{
    while (loop)
    {
        int ret = (params.read_or_write == "write") ? pwrite(params.fd, buffer, params.page_size, offsets[loops]) : pread(params.fd, buffer, params.page_size, offsets[loops]);
        loops++;
        if (ret < 0)
        {
            std::cerr << "Error during operation: " << strerror(errno) << "\n";
            break;
        }
        if (loops % granularity == 0 && get_current_time_ns() - start_time > 1e9)
        {
            loop = false;
        }
    }
}

void io_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    // Generate offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // allocate buffer
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    if (params.read_or_write == "write")
    {
        __builtin_prefetch(buffer, 1, 3);
    }
    else
    {
        __builtin_prefetch(buffer, 0, 3);
    }

    // Measure preads that can be done in 1 second for time refresh
    uint64_t loops = 0;

    uint64_t start_time = get_current_time_ns();
    bool loop = true;
    sync_refresh_interval_acquire(params, buffer, offsets, loops, loop, start_time, 1000);
    uint64_t end_time = get_current_time_ns();

    double submission_time = 0;
    double completion_time = 0;
    int max_retries = 5; // Maximum number of retries


    int ret = 0;
    std::ostringstream stats_buffer; // Buffer to accumulate stats output
    uint64_t increments = loops;

    start_time = get_current_time_ns();

    if (params.read_or_write == "write")
    {
        for (stats.io_completed = 0; stats.io_completed < params.io; stats.io_completed++)
        {
            while (ret < params.page_size)
            {
                ret +=  pwrite(params.fd, buffer + ret, params.page_size - ret,
                                            offsets[stats.io_completed] + ret);

            }
            ret = 0;
            if (stats.io_completed == loops)
            {
                loops += increments;

                uint64_t completion_time = get_current_time_ns();

                double elapsed_time = (completion_time - start_time) / 1e9;
                double iops = stats.io_completed / elapsed_time;
                double throughput = (stats.io_completed * params.page_size) / elapsed_time;

                stats_buffer.str("");
                stats_buffer << "\rThread " << thread_id
                             << ": IOPS = " << std::fixed << std::setprecision(2) << iops
                             << ", Throughput = " << throughput / 1e6 << " MB/s"
                             << ", Elapsed Time = " << std::fixed << std::setprecision(2)
                             << (elapsed_time) << "s";

                std::cout << stats_buffer.str() << std::flush;
            }
        }
    }
    else
    {
        for (stats.io_completed = 0; stats.io_completed < params.io; stats.io_completed++)
        {
            while (ret < params.page_size)
            {
                ret += pread(params.fd, buffer + ret, params.page_size - ret,
                                           offsets[stats.io_completed] + ret);

            }
            ret = 0;
            if (stats.io_completed == loops)
            {
                loops += increments;

                uint64_t completion_time = get_current_time_ns();

                double elapsed_time = (completion_time - start_time) / 1e9;
                double iops = stats.io_completed / elapsed_time;
                double throughput = (stats.io_completed * params.page_size) / elapsed_time;

                stats_buffer.str("");
                stats_buffer << "\rThread " << thread_id
                             << ": IOPS = " << std::fixed << std::setprecision(2) << iops
                             << ", Throughput = " << throughput / 1e6 << " MB/s"
                             << ", Elapsed Time = " << std::fixed << std::setprecision(2)
                             << (elapsed_time) << "s";

                std::cout << stats_buffer.str() << std::flush;
            }
        }
    }
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;

    end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;

    // Free buffer
    free(buffer);
}

void time_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{


    // Generate initial offsets
    std::vector<uint64_t> offsets = generate_offsets(params, thread_id);

    // Allocate a buffer aligned to the page size
    char *buffer = nullptr;
    if (posix_memalign((void **)&buffer, params.page_size, params.page_size) != 0)
    {
        throw std::runtime_error("Error allocating buffer: " + std::string(strerror(errno)));
    }

    if (params.read_or_write == "write")
    {
        __builtin_prefetch(buffer, 1, 3);
    }
    else
    {
        __builtin_prefetch(buffer, 0, 3);
    }

    // Measure preads that can be done in 1 second for time refresh
    uint64_t loops = 0;
    int ret = 0;

    uint64_t start_time = get_current_time_ns();
    bool loop = true;
    sync_refresh_interval_acquire(params, buffer, offsets, loops, loop, start_time, 100);
    uint64_t end_time = get_current_time_ns();

    // Adjust the number of operations based on measured performance
    params.io = loops * params.duration * 2;       // Double it to accommodate fluctuations
    offsets = generate_offsets(params, thread_id); // Regenerate offsets

    std::ostringstream stats_buffer; // Buffer to accumulate stats output

    uint64_t counter = 0;

    start_time = get_current_time_ns();

    loop = true;
    if (params.read_or_write == "write")
    {
        while (loop)
        {

            while (ret < params.page_size)
            {
                ret += pwrite(params.fd, buffer + ret, params.page_size - ret,
                                            offsets[stats.io_completed + counter] + ret);
            }
            counter++;
            ret = 0;
            if (counter == loops)
            {

                stats.io_completed += loops;
                counter = 0;

                uint64_t completion_time = get_current_time_ns();

                if (completion_time - end_time > 1e9 * params.duration)
                {
                    loop = false;
                    break;
                }
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
            }
        }
    }
    else
    {
        while (loop)
        {
            while (ret < params.page_size)
            {
                ret += pread(params.fd, buffer + ret, params.page_size - ret,
                                           offsets[stats.io_completed + counter] + ret);
            }
            counter++;
            ret = 0;
            if (counter == loops)
            {
                stats.io_completed += loops;
                counter = 0;

                uint64_t completion_time = get_current_time_ns();

                if (completion_time - start_time > 1e9 * params.duration)
                {
                    loop = false;
                    break;
                }
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
            }
        }
    }

    // Calculate total time taken
    end_time = get_current_time_ns();
    stats.total_time = (end_time - start_time) / 1e9;
    std::cout << "\r" << std::string(stats_buffer.str().length(), ' ') << "\r" << std::flush;

    // Free the allocated buffer
    free(buffer);
}
