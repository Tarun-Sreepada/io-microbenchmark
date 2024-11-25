#pragma once
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
#include <random>
#include <string>
#include <mutex>
#include <cerrno>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <algorithm>
#include <numeric>
#include <csignal>

#include <sstream>


#define KIBI 1024LL
#define KILO 1000LL

struct benchmark_params
{
    std::string location;
    int page_size = 4096;
    std::string seq_or_rand = "seq";
    std::string read_or_write = "read";
    uint64_t io = 50000;
    bool time_based = false; // New field for time-based mode
    uint64_t duration = 0;   // Duration in seconds for time-based benchmark
    bool skip_confirmation = false;
    ssize_t device_size = 0;
    uint64_t threads = 1;
    uint64_t queue_depth = 1;
    uint64_t refresh_interval = 1e8; // 100ms
    std::string engine = "sync";

    int fd = -1;
    char *buf = nullptr;
    std::vector<uint64_t> offsets;
    uint64_t total_num_pages = 0;
    uint64_t data_size = 0;
};

struct thread_stats
{
    uint64_t io_completed;
    double total_time;
};

uint64_t get_current_time_ns();

unsigned long long get_device_size(int fd);
std::string byte_conversion(unsigned long long bytes, const std::string &unit);
void print_help(const char *program_name);
benchmark_params parse_arguments(int argc, char *argv[]);

std::vector<uint64_t> generate_offsets(const benchmark_params &params, uint64_t thread_id);