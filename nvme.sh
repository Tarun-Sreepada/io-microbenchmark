#!/bin/bash

# Check if nvme-cli is installed
if ! command -v nvme &> /dev/null; then
    echo "nvme-cli is not installed. Please install it using 'sudo apt install nvme-cli'."
    exit 1
fi

# Specify your NVMe device (e.g., /dev/nvme0)
NVME_DEVICE="/dev/nvme0"

# Fetch and display maximum queue depth
MAX_QUEUE_DEPTH=$(nvme id-ctrl "$NVME_DEVICE" | grep "q_depth" | awk '{print $3}')

if [ -z "$MAX_QUEUE_DEPTH" ]; then
    echo "Could not retrieve queue depth. Please ensure the device is correct and accessible."
else
    echo "The maximum queue depth of $NVME_DEVICE is: $MAX_QUEUE_DEPTH"
fi
