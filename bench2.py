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
queue_depths = [1]  # Focusing on queue depth of 1 for comparison
page_sizes = [512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072]
location = '/dev/nvme0n1'  # Update with your device
io = 20000
threads = 1  # Use a single thread for this comparison
executables = {
    'async': '/export/home1/ltarun/io-microbenchmark/build/io_benchmark',
    'sync': '/export/home1/ltarun/io-microbenchmark/build/io_benchmark_sync'
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
df.to_csv('benchmark_results_sync_vs_async.csv', index=False)

# Set up the plotting style
sns.set(style="whitegrid")
palette = sns.color_palette("muted")

# Plot IOPS vs Page Size for Sync and Async
for op_type in operations:
    for method in methods:
        plt.figure()
        subset_async = df[(df['execution'] == 'async') & (df['operation'] == op_type) & (df['method'] == method)]
        subset_sync = df[(df['execution'] == 'sync') & (df['operation'] == op_type) & (df['method'] == method)]
        
        if not subset_async.empty:
            plt.plot(subset_async['page_size'], subset_async['iops'], marker='o', label='Async')
        if not subset_sync.empty:
            plt.plot(subset_sync['page_size'], subset_sync['iops'], marker='x', label='Sync')
        
        plt.title(f'IOPS vs Page Size ({op_type.capitalize()}, {method}, QD={queue_depths[0]})')
        plt.xlabel('Page Size (Bytes)')
        plt.ylabel('IOPS')
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend()
        plt.savefig(f'iops_sync_vs_async_{op_type}_{method}.png')
        plt.close()

# Similarly for Bandwidth and Average Latency
for op_type in operations:
    for method in methods:
        plt.figure()
        subset_async = df[(df['execution'] == 'async') & (df['operation'] == op_type) & (df['method'] == method)]
        subset_sync = df[(df['execution'] == 'sync') & (df['operation'] == op_type) & (df['method'] == method)]
        
        if not subset_async.empty:
            plt.plot(subset_async['page_size'], subset_async['bandwidth'], marker='o', label='Async')
        if not subset_sync.empty:
            plt.plot(subset_sync['page_size'], subset_sync['bandwidth'], marker='x', label='Sync')
        
        plt.title(f'Bandwidth vs Page Size ({op_type.capitalize()}, {method}, QD={queue_depths[0]})')
        plt.xlabel('Page Size (Bytes)')
        plt.ylabel('Bandwidth (MB/s)')
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend()
        plt.savefig(f'bandwidth_sync_vs_async_{op_type}_{method}.png')
        plt.close()

for op_type in operations:
    for method in methods:
        plt.figure()
        subset_async = df[(df['execution'] == 'async') & (df['operation'] == op_type) & (df['method'] == method)]
        subset_sync = df[(df['execution'] == 'sync') & (df['operation'] == op_type) & (df['method'] == method)]
        
        if not subset_async.empty:
            plt.plot(subset_async['page_size'], subset_async['avg_latency'], marker='o', label='Async')
        if not subset_sync.empty:
            plt.plot(subset_sync['page_size'], subset_sync['avg_latency'], marker='x', label='Sync')
        
        plt.title(f'Average Latency vs Page Size ({op_type.capitalize()}, {method}, QD={queue_depths[0]})')
        plt.xlabel('Page Size (Bytes)')
        plt.ylabel('Average Latency (μs)')
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend()
        plt.savefig(f'avg_latency_sync_vs_async_{op_type}_{method}.png')
        plt.close()

print("Benchmarking and plotting completed.")
