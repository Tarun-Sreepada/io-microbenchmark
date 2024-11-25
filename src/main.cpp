#include "config.h"
#include "sync.h"
#include "async.h"
#include "iou.h"

int main(int argc, char *argv[])
{
    benchmark_params params = parse_arguments(argc, argv);

    std::vector<thread_stats> thread_stats_list(params.threads);
    std::vector<std::thread> threads;

    for (uint64_t i = 0; i < params.threads; ++i)
    {
        if (params.engine == "sync")
        {
            if (params.time_based)
            {
                threads.push_back(std::thread(time_benchmark_thread_sync, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
            else
            {
                threads.push_back(std::thread(io_benchmark_thread_sync, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
        }
        else if (params.engine == "liburing")
        {
            if (params.time_based)
            {
                threads.push_back(std::thread(time_benchmark_thread_async, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
            else
            {
                threads.push_back(std::thread(io_benchmark_thread_async, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
        }
        else if (params.engine == "io_uring")
        {
            if (params.time_based)
            {
                threads.push_back(std::thread(time_benchmark_thread_iou, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
            else
            {
                threads.push_back(std::thread(io_benchmark_thread_iou, std::ref(params), std::ref(thread_stats_list[i]), i));
            }
        }
        else
        {
            std::cerr << "Invalid engine specified\n";
            exit(EXIT_FAILURE);
        }
    }

    // wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    // calculate total statistics
    uint64_t total_io_completed = 0;
    double total_time = 0;

    for (const auto &stats : thread_stats_list)
    {
        total_io_completed += stats.io_completed;
        total_time = std::max(total_time, stats.total_time);
    }

    double throughput = double(total_io_completed) / total_time;

    double total_data_size = total_io_completed * params.page_size;
    double total_data_size_MB = total_data_size / (KILO * KILO);

    std::cout << "Total I/O Completed: " << total_io_completed
              << "\nTotal Data Size: " << total_data_size_MB << " MB"
              << "\nTotal Time: " << total_time << " seconds"
              << "\nThroughput: " << throughput << " IOPS"
              << "\nBandwidth: " << total_data_size_MB / total_time << " MB/s" << std::endl;


    close(params.fd);
    return EXIT_SUCCESS;
}