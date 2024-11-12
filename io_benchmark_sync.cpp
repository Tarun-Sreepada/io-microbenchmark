#include <iostream>
#include <getopt.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <sys/stat.h>
#include <stdexcept>
#include <fcntl.h>
#include <ctime>
#include <vector>
#include <cmath>
#include <chrono>
#include <queue>
#include <thread>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <cerrno>
#include <linux/fs.h>
#include <stdexcept>
#include <sys/ioctl.h>

#define KIBI 1024LL
#define KILO 1000LL

unsigned long long get_device_size(int fd) {
    unsigned long long size;
    if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
        throw std::runtime_error("Failed to get device size using ioctl: " + std::string(strerror(errno)));
    }
    return size;
}

struct benchmark_params {
    std::string location;
    int page_size = 4096;
    std::string seq_or_rand = "seq";
    std::string read_or_write = "read";
    uint64_t io = 10000;
    bool skip_confirmation = false;
    ssize_t device_size = 0;
    uint64_t threads = 1;

    int fd = -1;
    char *buf = nullptr;
    std::vector<uint64_t> offsets;
    uint64_t total_num_pages = 0;
    uint64_t data_size = 0;
};

void print_help(const char *program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "Options:\n"
              << "  --location=<location>       Device location (required, e.g., /dev/sda)\n"
              << "  --page_size=<size>          Page size (default: 4096)\n"
              << "  --method=<seq|rand>         Access method (default: seq)\n"
              << "  --type=<read|write>         Operation type (default: read)\n"
              << "  --io=<value>                Number of IO requests (default: 10000)\n"
              << "  --threads=<threads>         Number of threads (default: 1)\n"
              << "  -y                          Skip confirmation\n"
              << "  --help                      Display this help message\n";
}
void io_benchmark_thread(benchmark_params params, uint64_t thread_id) {
    // Open the device/file per thread
    int fd = open(params.location.c_str(), (params.read_or_write == "read") ? O_RDONLY | O_DIRECT : O_RDWR | O_DIRECT);
    if (fd == -1) {
        std::cerr << "Thread " << thread_id << " - Error opening device: " << strerror(errno) << std::endl;
        return;
    }

    // Allocate buffer per thread
    void* buf;
    if (posix_memalign(&buf, params.page_size, params.page_size) != 0) {
        std::cerr << "Thread " << thread_id << " - Error allocating aligned memory\n";
        close(fd);
        return;
    }

    // Generate offsets
    std::vector<uint64_t> offsets(params.io);
    if (params.seq_or_rand == "seq") {
        for (uint64_t i = 0; i < params.io; ++i) {
            offsets[i] = ((i * params.page_size) + thread_id * params.page_size) % params.device_size;
        }
    } else if (params.seq_or_rand == "rand") {
        std::mt19937_64 rng(std::random_device{}() + thread_id);
        std::uniform_int_distribution<uint64_t> dist(0, params.total_num_pages - 1);
        for (uint64_t i = 0; i < params.io; ++i) {
            offsets[i] = dist(rng) * params.page_size;
        }
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t io_completed = 0;
    uint64_t min_latency = std::numeric_limits<uint64_t>::max();
    uint64_t max_latency = 0;
    double total_latency = 0.0;  // Total latency in microseconds

    for (uint64_t i = 0; i < params.io; ++i) {
        uint64_t offset = offsets[i];
        auto start = std::chrono::high_resolution_clock::now();
        ssize_t bytes;

        if (params.read_or_write == "read") {
            bytes = pread(fd, buf, params.page_size, offset);
            if (bytes != params.page_size) {
                std::cerr << "Thread " << thread_id << " - Read error: expected " << params.page_size << ", got " << bytes << std::endl;
            }
        } else {
            memset(buf, 'A', params.page_size);
            bytes = pwrite(fd, buf, params.page_size, offset);
            if (bytes != params.page_size) {
                std::cerr << "Thread " << thread_id << " - Write error: expected " << params.page_size << ", got " << bytes << std::endl;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();

        io_completed++;
        uint64_t latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        total_latency += latency;

        if (latency < min_latency) {
            min_latency = latency;
        }
        if (latency > max_latency) {
            max_latency = latency;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(end_time - start_time).count();

    double throughput = io_completed / total_time;
    double total_data_size = io_completed * params.page_size;
    double total_data_size_MB = total_data_size / (1024.0 * 1024.0);
    double avg_latency = total_latency / io_completed;  // Average latency in microseconds

    std::cout << "Thread " << thread_id << " Results:"
              << "\nTotal I/O Completed: " << io_completed
              << "\nTotal Data Size: " << total_data_size_MB << " MB"
              << "\nTotal Time: " << total_time << " seconds"
              << "\nThroughput: " << throughput << " IOPS"
              << "\nThroughput: " << total_data_size_MB / total_time << " MB/s"
              << "\nAverage Latency: " << avg_latency << " microseconds"
              << "\nMin Latency: " << min_latency << " microseconds"
              << "\nMax Latency: " << max_latency << " microseconds" << std::endl;

    free(buf);
    close(fd);
}

int main(int argc, char *argv[]) {
    benchmark_params params;

    struct option long_options[] = {
        {"location", required_argument, nullptr, 'l'},
        {"page_size", required_argument, nullptr, 'p'},
        {"method", required_argument, nullptr, 'm'},
        {"type", required_argument, nullptr, 't'},
        {"io", required_argument, nullptr, 'i'},
        {"threads", required_argument, nullptr, 'n'},
        {"help", no_argument, nullptr, 'h'},
        {"skip_confirmation", no_argument, nullptr, 'y'},
        {"queue_depth", required_argument, nullptr, 'q'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;

    int temp;

    while ((opt = getopt_long(argc, argv, "l:p:m:t:i:n:hy", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'l': params.location = optarg; break;
            case 'p': params.page_size = std::stoi(optarg); break;
            case 'm': params.seq_or_rand = optarg; break;
            case 't': params.read_or_write = optarg; break;
            case 'i': params.io = std::stoull(optarg); break;
            case 'n': params.threads = std::stoull(optarg); break;
            case 'y': params.skip_confirmation = true; break;
            case 'q': temp = std::stoull(optarg); break;
            case 'h': print_help(argv[0]); return 0;
            default: std::cerr << "Invalid option. Use --help for usage information.\n"; return 1;
        }
    }

    if (params.location.empty()) {
        std::cerr << "Error: --location is required.\n";
        print_help(argv[0]);
        return 1;
    }

    params.fd = open(params.location.c_str(), O_RDWR | O_DIRECT);
    if (params.fd == -1) {
        std::cerr << "Error opening device: " << strerror(errno) << std::endl;
        return 1;
    }

    params.device_size = get_device_size(params.fd);

    if (posix_memalign((void **)&params.buf, params.page_size, params.page_size) != 0) {
        close(params.fd);
        std::cerr << "Error allocating aligned memory\n";
        return 1;
    }

    params.total_num_pages = params.device_size / params.page_size;
    params.data_size = params.io * params.page_size;

    // Start threads
    std::vector<std::thread> threads;
    for (uint64_t i = 0; i < params.threads; ++i) {
        threads.emplace_back(io_benchmark_thread, std::ref(params), i);
    }

    for (auto &t : threads) {
        t.join();
    }

    free(params.buf);
    close(params.fd);

    return 0;
}
