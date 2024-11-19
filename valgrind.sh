#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e
# sudo fio --name=test --rw=read --ioengine=sync --direct=1 --iodepth=32 --bs=4k --numjobs=1 --filename=/dev/nvme0n1 --ramp_time=10s --runtime=60

# Variables
IO_BENCHMARK="./build/io_benchmark"
LOCATION="/dev/nvme0n1"
QUEUE_DEPTH=1
IO=1000000
METHOD="seq"
TYPE="read"
THREADS=1
CALLGRIND_OUT="/tmp/callgrind.out.$(date +%s)"  # Unique filename based on timestamp

# Check if io_benchmark executable exists
if [ ! -x "$IO_BENCHMARK" ]; then
    echo "Error: $IO_BENCHMARK not found or not executable."
    exit 1
fi

# Run Valgrind with Callgrind
echo "Running Valgrind Callgrind profiling..."
valgrind --tool=callgrind --callgrind-out-file="$CALLGRIND_OUT" \
    "$IO_BENCHMARK" \
    --location="$LOCATION" \
    --queue_depth="$QUEUE_DEPTH" \
    --io="$IO" \
    --method="$METHOD" \
    --threads="$THREADS" \
    --type="$TYPE" -y \
    --sync

echo "Valgrind profiling completed. Output file: $CALLGRIND_OUT"

# Check if the output file was created
if [ ! -f "$CALLGRIND_OUT" ]; then
    echo "Error: Callgrind output file not found."
    exit 1
fi

# Check if KCachegrind is installed
if ! command -v kcachegrind &> /dev/null; then
    echo "Error: KCachegrind is not installed. Please install it to visualize the profiling data."

    # run callgrind_annotate to show the profiling data in the terminal
    echo "Showing profiling data in profile.txt..."
    callgrind_annotate "$CALLGRIND_OUT" > profile.txt

    # delete the output file
    rm "$CALLGRIND_OUT"

    exit 1
fi

# Open the Callgrind output with KCachegrind
echo "Opening KCachegrind..."
kcachegrind "$CALLGRIND_OUT" &

echo "KCachegrind is now running."

exit 0
