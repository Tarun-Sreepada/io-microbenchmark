import subprocess
import re
import matplotlib.pyplot as plt
import itertools
import os
import pandas as pd
import seaborn as sns

# Define the parameter combinations
operations = ['read', 'write']
methods = ['seq', 'rand']
# queue_depths = [1,2,128,512,1024,4096,8192,16384]  # Focusing on queue depth of 1 for comparison
queue_depths = [1,2,8,16]
page_sizes = [4096]
location = '/dev/nvme0n1'  # Update with your device
io = 10000
threads = 6  # Use a single thread for this comparison
executables = {
    'async': os.path.join(os.path.dirname(os.path.realpath(__file__)), 'build/io_benchmark'),
    'sync': os.path.join(os.path.dirname(os.path.realpath(__file__)), 'build/io_benchmark_sync')
}

# Check if the script is run as root
if os.geteuid() != 0:
    print("This script needs to be run as root.")
    exit(1)

results = []

# Iterate over all combinations
for op_type, method, qd, page_size, (exec_type, exec_path) in itertools.product(
    operations, methods, queue_depths, page_sizes, executables.items()):
    print(f"Running benchmark: Exec={exec_type}, Type={op_type}, Method={method}, Queue Depth={qd}, Page Size={page_size}")
    cmd = [
        f'{exec_path}',
        f'--location={location}',
        f'--page_size={page_size}',
        f'--method={method}',
        f'--type={op_type}',
        f'--io={io}',
        f'--threads={threads}',
        f'--queue_depth={qd}',
        '-y'  # Skip confirmation for write operations
    ]
    
    # Run the command and capture the output
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, universal_newlines=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running command: {e.output}")
        continue

    # Parse the output to extract metrics
    total_io = None
    total_time = None
    iops = None
    bandwidth = None
    min_latency = None
    max_latency = None
    avg_latency = None

    # Use regular expressions to extract values
    total_io_match = re.search(r'Total I/O Completed:\s+(\d+)', output)
    if total_io_match:
        total_io = int(total_io_match.group(1))

    total_time_match = re.search(r'Total Time:\s+([\d\.]+)\s+seconds', output)
    if total_time_match:
        total_time = float(total_time_match.group(1))

    # Handle scientific notation in IOPS
    iops_match = re.search(r'Throughput:\s+([\d\.]+(?:e\+[\d]+)?)\s+IOPS', output)
    if iops_match:
        iops_str = iops_match.group(1)
        iops = float(iops_str)

    bandwidth_match = re.search(r'Throughput:\s+([\d\.]+)\s+MB/s', output)
    if bandwidth_match:
        bandwidth = float(bandwidth_match.group(1))

    avg_latency_match = re.search(r'Average Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output)
    if avg_latency_match:
        avg_latency_str = avg_latency_match.group(1)
        avg_latency = float(avg_latency_str)

    min_latency_match = re.search(r'Min Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output)
    if min_latency_match:
        min_latency_str = min_latency_match.group(1)
        min_latency = float(min_latency_str)

    max_latency_match = re.search(r'Max Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output)
    if max_latency_match:
        max_latency_str = max_latency_match.group(1)
        max_latency = float(max_latency_str)

    # Check if all metrics were successfully extracted
    if None in [total_io, total_time, iops, bandwidth, min_latency, max_latency, avg_latency]:
        print("Failed to parse some metrics from output.")
        print([total_io, total_time, iops, bandwidth, min_latency, max_latency, avg_latency])
        print(output)
        continue

    # Store the results
    results.append({
        'execution': exec_type,
        'operation': op_type,
        'method': method,
        'queue_depth': qd,
        'page_size': page_size,
        'iops': iops,
        'bandwidth': bandwidth,
        'min_latency': min_latency,
        'max_latency': max_latency,
        'avg_latency': avg_latency,
        'total_time': total_time,
        'total_io': total_io
    })

# Organize the results for plotting
df = pd.DataFrame(results)


# Save the results to a CSV file
df.to_csv(f'benchmark_results_sync_vs_async.csv', index=False)


# Set up the plotting style
sns.set(style="whitegrid")
palette = sns.color_palette("muted")

# Iterate through each operation and method
for op_type in operations:
    for method in methods:
        
        # Loop through page sizes
        for page_size in page_sizes:
            fig, axs = plt.subplots(3, 1, figsize=(10, 15))  # Create a figure with 3 subplots for IOPS, Bandwidth, and Latency
            fig.suptitle(f'Metrics vs Queue Depth ({op_type.capitalize()}, {method}, Page Size={page_size})')

            # Prepare subsets for async and sync data
            subset_async = df[(df['execution'] == 'async') & (df['operation'] == op_type) & (df['method'] == method) & (df['page_size'] == page_size)]
            subset_sync = df[(df['execution'] == 'sync') & (df['operation'] == op_type) & (df['method'] == method) & (df['page_size'] == page_size)]
            
            metrics = [('IOPS', 'iops'), ('Bandwidth (MB/s)', 'bandwidth'), ('Average Latency (μs)', 'avg_latency')]

            for idx, (metric_label, metric_col) in enumerate(metrics):
                ax = axs[idx]
                
                # Plot Sync and Async data for each metric
                if not subset_async.empty:
                    ax.plot(subset_async['queue_depth'], subset_async[metric_col], marker='o', label='Async')
                if not subset_sync.empty:
                    ax.plot(subset_sync['queue_depth'], subset_sync[metric_col], marker='x', label='Sync')
                
                ax.set_title(f'{metric_label} vs Queue Depth')
                ax.set_xlabel('Queue Depth')
                ax.set_ylabel(metric_label)
                ax.grid(True)

                ax.set_xscale('log', base=2)
                ax.legend()

                # Calculate and plot the ratio between sync and async
                if not subset_sync.empty and not subset_async.empty:
                    ratio = subset_async[metric_col].values / subset_sync[metric_col].values
                    ax_ratio = ax.twinx()
                    ax_ratio.plot(subset_sync['queue_depth'], ratio, color='green', linestyle='dashed', marker='s', label='Sync/Async')
                    ax_ratio.set_ylabel(f'Sync/Async {metric_label} Ratio')
                    ax_ratio.legend(loc='upper right')

            # Save the combined plot to a single file
            plt.tight_layout(rect=[0, 0.03, 1, 1])  # Adjust layout to make room for the main title
            plt.savefig(f'combined_metrics_{op_type}_{method}_{page_size}.png')
            plt.close(fig)

print("Benchmarking and plotting with combined metrics completed.")


