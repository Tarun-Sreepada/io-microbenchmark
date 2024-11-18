#include <iostream>
#include <getopt.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdexcept>
#include <fcntl.h>
#include <linux/fs.h>
#include <ctime>
#include <vector>
#include <cmath>
#include <chrono>
#include <queue>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <cerrno>
#include <liburing.h>
#include <x86intrin.h>
#include <numeric>
#include <algorithm>

#define KIBI 1024LL
#define KILO 1000LL

#include <time.h>

uint64_t get_current_time_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1e9 + ts.tv_nsec;
}

unsigned long long get_device_size(int fd)
{
    unsigned long long size;
    if (ioctl(fd, BLKGETSIZE64, &size) == -1)
    {
        throw std::runtime_error("Failed to get device size using ioctl: " + std::string(strerror(errno)));
    }

    return size;
}

struct benchmark_params
{
    std::string location;               // Block device location (e.g., /dev/sda)
    int page_size = 4096;               // Default page size
    std::string seq_or_rand = "seq";    // Default method: sequential
    std::string read_or_write = "read"; // Default type: read
    uint64_t io = 10000;                // Default IO
    bool skip_confirmation = false;     // Default: do not skip confirmation
    ssize_t device_size = 0;            // Size of the device (default: 0)
    uint64_t threads = 1;               // Default number of threads
    uint64_t queue_depth = 1;           // Default queue depth

    int fd = -1;                    // File descriptor for the device
    char *buf = nullptr;            // Buffer for I/O operations
    std::vector<uint64_t> offsets;  // Offsets for I/O operations
    uint64_t total_num_pages = 0;   // Total number of pages to benchmark
    uint64_t data_size = 0;         // Total data size to read/write
    uint64_t total_time = 0;        // Total time taken for the benchmark
    uint64_t max_response_time = 0; // Maximum response time recorded
};

void print_help(const char *program_name)
{
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "Options:\n"
              << "  --location=<location>       Device location (required, e.g., /dev/sda)\n"
              << "  --page_size=<size>          Page size (default: 4096)\n"
              << "  --method=<seq|rand>         Access method (default: seq)\n"
              << "  --type=<read|write>         Operation type (default: read)\n"
              << "  --io=<value>                Number of IO requests (default: 10000)\n"
              << "  --threads=<threads>         Number of threads (default: 1)\n"
              << "  --queue_depth=<depth>       Queue depth (default: 1)\n"
              << "  -y                          Skip confirmation\n"
              << "  --help                      Display this help message\n";
}

struct thread_stats
{
    uint64_t io_completed;
    double total_latency; // Sum of latencies in microseconds
    double min_latency;
    double max_latency;
    double total_time; // Total time in seconds
};

std::string byte_conversion(unsigned long long bytes, const std::string &unit)
{
    const std::string binary[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    const std::string metric[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    const auto &units = (unit == "binary") ? binary : metric;
    int i = 0;
    unsigned long long base = (unit == "binary") ? KIBI : KILO;

    while (bytes >= base && i < 5)
    {
        bytes /= base;
        i++;
    }

    return std::to_string(static_cast<int>(round(bytes))) + " " + units[i];
}

inline void io_uring_prep_read_or_write(
    struct io_uring_sqe *sqe, int fd, struct iovec *iovec_ptr, unsigned nr_vecs, off_t offset, const std::string &op_type)
{
    if (op_type == "read")
    {
        io_uring_prep_readv(sqe, fd, iovec_ptr, nr_vecs, offset);
    }
    else
    {
        io_uring_prep_writev(sqe, fd, iovec_ptr, nr_vecs, offset);
    }
}

inline void handle_cqe(struct io_uring_cqe *cqe, uint64_t thread_id, benchmark_params &params)
{
    if (cqe->res < 0)
    {
        std::cerr << "Thread " << thread_id << " - I/O operation failed: " << strerror(-cqe->res) << std::endl;
    }
    else if ((size_t)cqe->res != params.page_size)
    {
        std::cerr << "Thread " << thread_id << " - Short I/O operation: expected " << params.page_size << ", got " << cqe->res << std::endl;
    }
}

void io_benchmark_thread(benchmark_params &params, thread_stats &stats, uint64_t thread_id)
{
    struct io_uring ring;

    struct io_uring_params params_ring;
    memset(&params_ring, 0, sizeof(params_ring));
    params_ring.flags = IORING_SETUP_IOPOLL;
    params_ring.sq_thread_idle = 1000;
    params_ring.sq_thread_cpu = thread_id % std::thread::hardware_concurrency(); // Pin to CPU
    params_ring.cq_entries = params.queue_depth * 2;                             // Adequate CQ entries
    params_ring.sq_entries = params.queue_depth;                                 // Adequate SQ entries
    int ret;

    // Initialize io_uring for this thread
    ret = io_uring_queue_init_params(params.queue_depth, &ring, &params_ring);
    if (ret < 0)
    {
        std::cerr << "Thread " << thread_id << " - io_uring_queue_init failed: " << strerror(-ret) << std::endl;
        exit(1);
    }

    // Bind the thread to a specific CPU
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(params_ring.sq_thread_cpu, &cpuset);
    ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0)
    {
        std::cerr << "Thread " << thread_id << " - Failed to set CPU affinity: " << strerror(ret) << std::endl;
        // Proceeding without affinity if setting fails
    }

    // Allocate buffers and iovecs
    std::vector<char *> buffers(params.queue_depth);
    std::vector<struct iovec> iovecs(params.queue_depth);
    for (uint64_t i = 0; i < params.queue_depth; ++i)
    {
        if (posix_memalign((void **)&buffers[i], params.page_size, params.page_size) != 0)
        {
            std::cerr << "Thread " << thread_id << " - Error allocating aligned memory\n";
            exit(1);
        }
        // Fill the buffer with data
        memset(buffers[i], 'A', params.page_size);

        // Initialize iovec
        iovecs[i].iov_base = buffers[i];
        iovecs[i].iov_len = params.page_size;
    }

    // Generate offsets
    std::vector<uint64_t> offsets(params.io);
    if (params.seq_or_rand == "seq")
    {
        for (uint64_t i = 0; i < params.io; ++i)
        {
            offsets[i] = ((i * params.page_size) + thread_id * params.page_size) % params.device_size;
        }
    }
    else if (params.seq_or_rand == "rand")
    {
        std::mt19937_64 rng(std::random_device{}() + thread_id);
        std::uniform_int_distribution<uint64_t> dist(0, params.total_num_pages - 1);
        for (uint64_t i = 0; i < params.io; ++i)
        {
            offsets[i] = dist(rng) * params.page_size;
        }
    }
    else
    {
        throw std::runtime_error("Invalid method: " + params.seq_or_rand);
    }

    // Initialize per-thread statistics
    stats.io_completed = 0;
    stats.total_latency = 0.0;
    stats.min_latency = 0.0;
    stats.max_latency = 0.0;

    struct io_uring_cqe *cqes[params.queue_depth];

    auto start_time = std::chrono::high_resolution_clock::now();

    uint64_t submitted = 0, completed = 0;
    std::vector<uint64_t> latencies(params.io);

    while (completed < params.io)
    {
        uint64_t available_sqe = io_uring_sq_space_left(&ring);
        available_sqe = std::min(available_sqe, params.io - submitted);

        auto current_time = get_current_time_ns();
        for (uint64_t i = 0; i < available_sqe; ++i)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                std::cerr << "Failed to get SQE" << std::endl;
                break;
            }

            uint64_t index = (submitted + i) % params.queue_depth;
            uint64_t offset = offsets[submitted + i];
            latencies[submitted + i] = current_time;

            // Prepare the I/O operation with a single iovec
            io_uring_prep_read_or_write(sqe, params.fd, &iovecs[index], 1, offset, params.read_or_write);

            io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(submitted + i));
        }
        submitted += available_sqe;

        ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            std::cerr << "Thread " << thread_id << " - io_uring_submit failed: " << strerror(-ret) << std::endl;
            exit(1);
        }

        // Wait for the number of completions equal to available_sqe
        unsigned int num_cqes = 0;
        while (num_cqes < available_sqe)
        {
            struct io_uring_cqe *cqe;
            ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0)
            {
                std::cerr << "Thread " << thread_id << " - io_uring_wait_cqe failed: " << strerror(-ret) << std::endl;
                exit(1);
            }

            handle_cqe(cqe, thread_id, params);

            auto location = reinterpret_cast<uint64_t>(io_uring_cqe_get_data(cqe));
            uint64_t completion_time = get_current_time_ns();

            latencies[location] = completion_time - latencies[location];

            io_uring_cqe_seen(&ring, cqe);
            num_cqes++;
        }

        completed += num_cqes;
    }

    stats.io_completed = completed;
    // Convert nanoseconds to microseconds
    for (uint64_t i = 0; i < params.io; ++i)
    {
        latencies[i] /= 1000;
    }
    stats.total_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    stats.min_latency = *std::min_element(latencies.begin(), latencies.end());
    stats.max_latency = *std::max_element(latencies.begin(), latencies.end());

    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0; // Convert to seconds

    // Clean up resources
    for (uint64_t i = 0; i < params.queue_depth; ++i)
    {
        free(buffers[i]);
    }

    io_uring_queue_exit(&ring);
}

int main(int argc, char *argv[])
{
    benchmark_params params;

    struct option long_options[] = {
        {"location", required_argument, nullptr, 'l'},
        {"page_size", required_argument, nullptr, 'p'},
        {"method", required_argument, nullptr, 'm'},
        {"type", required_argument, nullptr, 't'},
        {"io", required_argument, nullptr, 'i'},
        {"threads", required_argument, nullptr, 'n'},
        {"queue_depth", required_argument, nullptr, 'q'},
        {"help", no_argument, nullptr, 'h'},
        {"skip_confirmation", no_argument, nullptr, 'y'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "l:p:m:t:i:n:q:hy", long_options, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'l':
            params.location = optarg;
            break;
        case 'p':
            params.page_size = std::stoi(optarg);
            break;
        case 'm':
            params.seq_or_rand = optarg;
            break;
        case 't':
            params.read_or_write = optarg;
            break;
        case 'i':
            params.io = std::stoull(optarg);
            break;
        case 'n':
            params.threads = std::stoull(optarg);
            break;
        case 'q':
            params.queue_depth = std::stoull(optarg);
            break;
        case 'y':
            params.skip_confirmation = true;
            break;
        case 'h':
            print_help(argv[0]);
            return 0;
        default:
            std::cerr << "Invalid option. Use --help for usage information.\n";
            return 1;
        }
    }

    if (params.location.empty())
    {
        std::cerr << "Error: --location is required.\n";
        print_help(argv[0]);
        return 1;
    }

    std::cout << "Location: " << params.location
              << "\tPage Size: " << params.page_size
              << "\tMethod: " << params.seq_or_rand
              << "\tType: " << params.read_or_write
              << "\tIO: " << params.io
              << "\tThreads: " << params.threads
              << "\tQueue Depth: " << params.queue_depth << std::endl;

    params.fd = open(params.location.c_str(), O_RDWR | O_DIRECT | O_SYNC);
    if (params.fd == -1)
    {
        std::cerr << "Error opening device: " << strerror(errno) << std::endl;
        return 1;
    }

    params.device_size = get_device_size(params.fd);

    if (params.read_or_write == "write" && !params.skip_confirmation)
    {
        std::cout << "\n\033[1;31m*** WARNING: Data Loss Risk ***\033[0m\n"
                  << "This will erase all data in: \033[1;31m" << params.location << "\033[0m\n"
                  << "Size: \033[1;31m" << byte_conversion(params.device_size, "binary")
                  << " (" << byte_conversion(params.device_size, "metric") << ")\033[0m\n"
                  << "Continue? (y/n): ";

        char response;
        std::cin >> response;
        if (response != 'y')
        {
            std::cout << "Write benchmark aborted.\n";
            return 0;
        }
    }

    if (posix_memalign((void **)&params.buf, params.page_size, params.page_size) != 0)
    {
        close(params.fd);
        std::cerr << "Error allocating aligned memory\n";
        return 1;
    }

    params.total_num_pages = params.device_size / params.page_size;
    params.data_size = params.io * params.page_size;

    std::vector<thread_stats> thread_stats_list(params.threads);

    std::vector<std::thread> threads;
    for (uint64_t i = 0; i < params.threads; ++i)
    {
        threads.emplace_back(io_benchmark_thread, std::ref(params), std::ref(thread_stats_list[i]), i);
    }

    // Wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    // Calculate total statistics
    uint64_t total_io_completed = 0;
    double total_latency = 0.0;
    double min_latency = std::numeric_limits<double>::max();
    double max_latency = 0.0;
    double total_time = 0.0;

    for (const auto &stats : thread_stats_list)
    {
        total_io_completed += stats.io_completed;
        total_latency += stats.total_latency;
        if (stats.min_latency < min_latency)
        {
            min_latency = stats.min_latency;
        }
        if (stats.max_latency > max_latency)
        {
            max_latency = stats.max_latency;
        }
        if (stats.total_time > total_time)
        {
            total_time = stats.total_time;
        }
    }

    double temp_time = total_time * KILO * KILO; // Convert to microseconds

    double avg_latency = total_latency / total_io_completed;
    double throughput = total_io_completed / total_time;

    double total_data_size = total_io_completed * params.page_size;
    double total_data_size_MB = total_data_size / (KILO * KILO);

    std::cout << "Total I/O Completed: " << total_io_completed
              << "\nTotal Data Size: " << total_data_size_MB << " MB"
              << "\nTotal Time: " << total_time << " seconds"
              << "\nThroughput: " << throughput << " IOPS"
              << "\nThroughput: " << total_data_size_MB / total_time << " MB/s"
              << "\nAverage Latency: " << avg_latency << " microseconds"
              << "\nMin Latency: " << min_latency << " microseconds"
              << "\nMax Latency: " << max_latency << " microseconds" << std::endl;

    // Close the device (if not opening per thread)
    close(params.fd);

    return 0;
}