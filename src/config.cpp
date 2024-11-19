#include "config.h"

uint64_t get_current_time_ns() {

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1e9 + ts.tv_nsec;

}

unsigned long long get_device_size(int fd) {
    unsigned long long size;
    if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
        throw std::runtime_error("Failed to get device size using ioctl: " + std::string(strerror(errno)));
    }
    return size;
}

benchmark_params parse_arguments(int argc, char *argv[]) {
    benchmark_params params;

    struct option long_options[] = {
        {"location", required_argument, nullptr, 'l'},
        {"page_size", required_argument, nullptr, 'p'},
        {"method", required_argument, nullptr, 'm'},
        {"type", required_argument, nullptr, 't'},
        {"io", required_argument, nullptr, 'i'},
        {"time", no_argument, nullptr, 'T'}, // New flag for time-based benchmarking
        {"duration", required_argument, nullptr, 'd'}, // New option for specifying duration
        {"threads", required_argument, nullptr, 'n'},
        {"queue_depth", required_argument, nullptr, 'q'},
        {"sync", no_argument, nullptr, 's'},
        {"async", no_argument, nullptr, 'a'},
        {"skip_confirmation", no_argument, nullptr, 'y'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    bool sync_flag_set, async_flag_set;
    while ((opt = getopt_long(argc, argv, "l:p:m:t:i:T:d:n:q:sayh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'l': params.location = optarg; break;
            case 'p': params.page_size = std::stoi(optarg); break;
            case 'm': params.seq_or_rand = optarg; break;
            case 't': params.read_or_write = optarg; break;
            case 'i': params.io = std::stoull(optarg); break;
            case 'T': params.time_based = true; break; // Enable time-based mode
            case 'd': params.duration = std::stoull(optarg); break; // Set duration
            case 'n': params.threads = std::stoull(optarg); break;
            case 'q': params.queue_depth = std::stoull(optarg); break;
            case 's': 
                params.use_sync = true; 
                std::cout << "Sync flag set\n";

                break;
            case 'a': 
                params.use_sync = false; 
                std::cout << "Async flag set\n";
                break;
            case 'y': params.skip_confirmation = true; break;
            case 'h': print_help(argv[0]); exit(0);
            default: 
                std::cerr << "Invalid option. Use --help for usage information.\n"; 
                exit(1);
        }
    }

    // Validate time-based parameters
    if (params.time_based) {
        if (params.duration == 0) {
            std::cerr << "Error: --time requires --duration to be specified with a non-zero value.\n";
            exit(1);
        }
        if (params.io != 10000) {
            std::cout << "Warning: --time mode overrides --io parameter.\n";
        }
    }

    if (!params.time_based && params.duration > 0) {
        std::cerr << "Error: --duration requires --time to be specified.\n";
        exit(1);
    }

    if (params.location.empty()) {
        std::cerr << "Error: --location is required.\n";
        print_help(argv[0]);
        exit(1);
    }

    if (!std::filesystem::exists(params.location)) {
        std::cerr << "Error: Device does not exist.\n";
        exit(1);
    }

    if (params.page_size <= 0) {
        std::cerr << "Error: Invalid page size.\n";
        exit(1);
    }

    if (params.io <= 0) {
        std::cerr << "Error: Invalid number of I/O requests.\n";
        exit(1);
    }

    if (params.threads <= 0) {
        std::cerr << "Error: Invalid number of threads.\n";
        exit(1);
    }

    if (params.queue_depth <= 0) {
        std::cerr << "Error: Invalid queue depth.\n";
        exit(1);
    }

    // if write add flag O_SYNC to ensure data is written to disk
    if (params.read_or_write == "write") {
        params.fd = open(params.location.c_str(), O_RDWR | O_DIRECT | O_SYNC);
    } else {
        params.fd = open(params.location.c_str(), O_RDONLY | O_DIRECT);
    }

    // If sync and queue depth is greater than 1, set queue depth to 1 and print warning
    if (params.use_sync && params.queue_depth > 1) {
        std::cout << "Warning: Queue depth is capped at 1 for synchronous I/O.\n";
        params.queue_depth = 1;
    }

    params.device_size = get_device_size(params.fd);

    // if write check if user is okay with data loss
    if (params.read_or_write == "write" && !params.skip_confirmation) {
        std::cout << "\n\033[1;31m*** WARNING: Data Loss Risk ***\033[0m\n"
                  << "This will erase all data in: \033[1;31m" << params.location << "\033[0m\n"
                  << "Size: \033[1;31m" << byte_conversion(params.device_size , "binary")
                  << " (" << byte_conversion(params.device_size , "metric") << ")\033[0m\n"
                  << "Continue? (y/n): ";

        char response;
        std::cin >> response;
        if (response != 'y') {
            std::cout << "Write benchmark aborted.\n";
            exit(0);
        }
    }


    if (params.fd == -1) {
        throw std::runtime_error("Error opening device: " + std::string(strerror(errno)));
    }

    std::cout << "Location: " << params.location
          << "\tPage Size: " << params.page_size
          << "\tMethod: " << params.seq_or_rand
          << "\tType: " << params.read_or_write;

    if (params.time_based) {
        std::cout << "\tExecution Type: Time-Based"
                << "\tDuration: " << params.duration << " seconds";
    } else {
        std::cout << "\tExecution Type: IO-Based"
                << "\tIO: " << params.io;
    }

    std::cout << "\tThreads: " << params.threads
            << "\tQueue Depth: " << params.queue_depth
            << "\tI/O Mode: " << (params.use_sync ? "Synchronous" : "Asynchronous") 
            << std::endl;


    return params;
}

void print_help(const char *program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "Options:\n"
              << "  --help                      Display this help message\n"
              << "  --location=<location>       Device location (required, e.g., /dev/sda)\n"
              << "  --page_size=<size>          Page size (default: 4096)\n"
              << "  --method=<seq|rand>         Access method (default: seq)\n"
              << "  --type=<read|write>         Operation type (default: read)\n"
              << "  --io=<value>                Number of IO requests (default: 10000)\n"
              << "  --threads=<threads>         Number of threads (default: 1)\n"
              << "  --queue_depth=<depth>       Queue depth (default: 1)\n"
              << "  --sync                      Use synchronous I/O\n"
              << "  --async                     Use asynchronous I/O\n"
              << "  -y                          Skip confirmation\n"
              << "  --time                      Enable time-based benchmarking\n"
              << "  --duration=<seconds>        Duration in seconds for time-based benchmarking\n";

              
}

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


std::vector<uint64_t> generate_offsets(const benchmark_params &params, uint64_t thread_id) {
    std::vector<uint64_t> offsets(params.io);
    uint64_t avoid = KILO * KILO *KILO; // Add 1GB to offset

    if (params.seq_or_rand == "seq") {
        for (uint64_t i = 0; i < params.io; ++i) {
            offsets[i] = ((i * params.page_size) + thread_id * params.page_size) % params.device_size + avoid;
        }
    } else if (params.seq_or_rand == "rand") {
        std::mt19937_64 rng(std::random_device{}() + thread_id);
        std::uniform_int_distribution<uint64_t> dist(0, params.total_num_pages - 1);
        for (uint64_t i = 0; i < params.io; ++i) {
            uint64_t offset = dist(rng) * params.page_size;
            // if the offset is within the avoid range, generate a new offset
            while (offset < avoid) {
                offset = dist(rng) * params.page_size;
            }
            offsets[i] = dist(rng) * params.page_size % params.device_size;
        }
    } else {
        throw std::runtime_error("Invalid method: " + params.seq_or_rand);
    }

    return offsets;
}
