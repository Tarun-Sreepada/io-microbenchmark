#pragma once
#include "config.h"

void io_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id);

void time_benchmark_thread_async(benchmark_params &params, thread_stats &stats, uint64_t thread_id);