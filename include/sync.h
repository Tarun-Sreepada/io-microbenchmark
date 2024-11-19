#pragma once
#include "config.h"

bool handle_write(int fd, char* buffer, uint64_t page_size, uint64_t offset);
bool handle_read(int fd, char* buffer, uint64_t page_size, uint64_t offset);   

void io_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id);

void time_benchmark_thread_sync(benchmark_params &params, thread_stats &stats, uint64_t thread_id);
