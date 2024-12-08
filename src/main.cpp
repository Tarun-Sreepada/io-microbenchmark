#include "config.h"
#include "sync.h"
#include "async.h"
#include "iou.h"

bool print = false;


enum class PrintMode {
    Individual,
    Cumulative,
    Both
};

void print_stats_thread(benchmark_params &params, 
                        std::vector<thread_stats> &thread_stats_list, 
                        std::chrono::milliseconds interval,
                        PrintMode print_mode) 
{
    std::vector<std::string> thread_outputs(params.threads); // Store outputs for each thread

    // Print initial lines
    if (print_mode == PrintMode::Individual || print_mode == PrintMode::Both) {
        for (size_t i = 0; i < params.threads; ++i) {
            std::cout << "-----" << std::endl;
            std::cout << "Thread " << i << ": Elapsed Time: 0s, IOPS: 0, Bandwidth: 0 MB/s" << std::endl;
            std::cout << "-----" << std::endl;
        }
    }
    if (print_mode == PrintMode::Cumulative || print_mode == PrintMode::Both) {
        std::cout << "-----" << std::endl;
        std::cout << "All Threads: Elapsed Time: 0s, IOPS: 0, Bandwidth: 0 MB/s" << std::endl;
        std::cout << "-----" << std::endl;
    }

    std::vector<uint64_t> last_io_count(params.threads, 0); // Store last recorded I/O count

while (print) 
{
    std::this_thread::sleep_for(interval);

    uint64_t current_time = get_current_time_ns();

    double bandwidth_sum = 0;

    // Calculate and store stats for each thread
    for (size_t i = 0; i < params.threads; ++i) 
    {
        const auto &stats = thread_stats_list[i];

        // Compute the number of I/Os since the last interval
        uint64_t io_diff = stats.io_completed - last_io_count[i];
        last_io_count[i] = stats.io_completed; // Update last recorded value

        // Calculate bandwidth for this interval (MB/s)
        double bandwidth = static_cast<double>(io_diff * params.page_size) / (interval.count() * KILO * KILO);

        bandwidth_sum += bandwidth;

        // Update the output for the current thread
        thread_outputs[i] = "Thread " + std::to_string(i) + 
                            ": IOPS: " + std::to_string(io_diff) + 
                            ", Bandwidth: " + std::to_string(bandwidth) + " MB/s";
    }

    // Print outputs based on the selected mode
    if (print_mode == PrintMode::Individual || print_mode == PrintMode::Both) {
        for (const auto &output : thread_outputs) {
            std::cout << output << std::endl;
        }
    }

    if (print_mode == PrintMode::Cumulative || print_mode == PrintMode::Both) {
        std::cout << "All Threads: IOPS: " 
                  << std::accumulate(last_io_count.begin(), last_io_count.end(), 0) 
                  << ", Bandwidth: " << bandwidth_sum << " MB/s" << std::endl;
    }

    std::cout << "-----" << std::endl;
}

}



int main(int argc, char *argv[])
{
    benchmark_params params = parse_arguments(argc, argv);

    std::vector<thread_stats> thread_stats_list(params.threads);
    std::vector<std::thread> threads;

    // launch a thread that constantly prints statistics every second
    print = true;
    std::thread stats_thread(print_stats_thread, std::ref(params), std::ref(thread_stats_list), std::chrono::milliseconds(500), PrintMode::Both);


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
    
    print = false;
    // clear the stats buffer
    std::cout << "\r" << std::string(params.stats_buffer.str().length(), ' ') << "\r" << std::flush;

    stats_thread.join();

    // calculate total statistics
    uint64_t total_io_completed = 0;
    double total_time = 0;

    for (const auto &stats : thread_stats_list)
    {
        total_io_completed += stats.io_completed;
        double time_elapsed = (stats.end_time - stats.start_time) / 1e9;

        total_time = std::max(total_time, time_elapsed);
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