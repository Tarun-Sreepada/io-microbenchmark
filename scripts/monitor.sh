#!/bin/bash

# Configuration
DEVICE="nvme0n1"  # NVMe device to monitor
BENCHMARK_CMD="/home/tarun/io-microbenchmark/build/io_benchmark --engine=io_uring --location=/dev/$DEVICE --time --duration=15 --page_size=4096 --method=rand --type=read --threads=1"
OUTPUT_DIR="./monitoring_logs"  # Directory to store logs
BENCHMARK_OUTPUT="$OUTPUT_DIR/benchmark_output.log"
DURATION=15  # Benchmark duration in seconds
MONITOR_DURATION=$((DURATION + 5))  # 3 seconds before + buffer

# Create output directory
mkdir -p $OUTPUT_DIR

# Start iostat in the background, filtered for the specific device
iostat -t -dx $DEVICE 1 $MONITOR_DURATION > "$OUTPUT_DIR/iostat_$DEVICE.log" &
IOSTAT_PID=$!

# Start sar to monitor CPU usage with timestamps
sar -P 0 1 $MONITOR_DURATION > "$OUTPUT_DIR/sar_cpu0.log" &
SAR_PID=$!

# Wait 3 seconds before starting the benchmark
echo "Waiting 2 seconds before starting the benchmark..."
sleep 2

# Run the benchmark
echo "Starting the benchmark..."
$BENCHMARK_CMD > $BENCHMARK_OUTPUT

# Wait for monitoring processes to finish
wait $IOSTAT_PID
wait $SAR_PID

echo "Monitoring completed. Logs are saved in $OUTPUT_DIR."
