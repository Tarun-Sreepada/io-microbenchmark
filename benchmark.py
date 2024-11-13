
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
queue_depths = [1, 4, 16, 64]
page_sizes = [4096, 65536]
location = '/dev/nvme0n1'  # Update with your device
io = 10000
threads = 6
# path is the location of this script + build directory
path = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'build/io_benchmark')

# Check if the script is run as root
if os.geteuid() != 0:
    print("This script needs to be run as root.")
    exit(1)

results = []

# Iterate over all combinations
for op_type, method, qd, page_size in itertools.product(operations, methods, queue_depths, page_sizes):
    print(f"Running benchmark: Type={op_type}, Method={method}, Queue Depth={qd}, Page Size={page_size}")
    cmd = [
        f'{path}',
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

    # First, try matching the standard decimal format for IOPS
    iops_match = re.search(r'Throughput:\s+([\d\.]+)\s+IOPS', output)
    if iops_match:
        iops = float(iops_match.group(1))

    # If the first match fails, try matching the scientific notation format
    if iops is None:
        iops_match = re.search(r'Throughput:\s+([\d\.]+)e\+([\d]+)\s+IOPS', output)
        if iops_match:
            iops = float(iops_match.group(1)) * pow(10, int(iops_match.group(2)))



    bandwidth_match = re.search(r'Throughput:\s+([\d\.]+)\s+MB/s', output)
    if bandwidth_match:
        bandwidth = float(bandwidth_match.group(1))

    avg_latency_match = re.search(r'Average Latency:\s+([\d\.]+)\s+microseconds', output)
    if avg_latency_match:
        avg_latency = float(avg_latency_match.group(1))

    min_latency_match = re.search(r'Min Latency:\s+([\d\.]+)\s+microseconds', output)
    if min_latency_match:
        min_latency = float(min_latency_match.group(1))

    max_latency_match = re.search(r'Max Latency:\s+([\d\.]+)\s+microseconds', output)
    if max_latency_match:
        max_latency = float(max_latency_match.group(1))

    if max_latency is None:
        max_latency_match = re.search(r'Max Latency:\s+([\d\.]+)e\+([\d]+)\s+microseconds', output)
        if max_latency_match:
            max_latency = float(max_latency_match.group(1)) * pow(10, int(max_latency_match.group(2)))
    
    # could be 1.20746e+06 microseconds so we need to handle this case
    if avg_latency is None:
        avg_latency_match = re.search(r'Average Latency:\s+([\d\.]+)e\+([\d]+)\s+microseconds', output)
        if avg_latency_match:
            avg_latency = float(avg_latency_match.group(1)) * pow(10, int(avg_latency_match.group(2)))
    

    # Check if all metrics were successfully extracted
    if None in [total_io, total_time, iops, bandwidth, min_latency, max_latency, avg_latency]:
        print("Failed to parse some metrics from output.")
        print([total_io, total_time, iops, bandwidth, min_latency, max_latency, avg_latency])
        print(output)
        continue

    # Store the results
    results.append({
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
df.to_csv('benchmark_results.csv', index=False)

# Set up the plotting style
sns.set(style="whitegrid")
palette = sns.color_palette("muted")

# Plot IOPS vs Queue Depth with multiple page sizes
for op_type in operations:
    for method in methods:
        plt.figure()
        for page_size in page_sizes:
            subset = df[(df['operation'] == op_type) & (df['method'] == method) & (df['page_size'] == page_size)]
            if subset.empty:
                continue
            plt.plot(subset['queue_depth'], subset['iops'], marker='o', label=f'{page_size}B')
        plt.title(f'IOPS vs Queue Depth ({op_type.capitalize()}, {method})')
        plt.xlabel('Queue Depth')
        plt.ylabel('IOPS')
        plt.xticks(queue_depths)
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend(title='Page Size')
        plt.savefig(f'iops_{op_type}_{method}.png')
        plt.close()

# Similarly for Bandwidth and Average Latency
# ... (Include similar code blocks for 'bandwidth' and 'avg_latency')

for op_type in operations:
    for method in methods:
        plt.figure()
        for page_size in page_sizes:
            subset = df[(df['operation'] == op_type) & (df['method'] == method) & (df['page_size'] == page_size)]
            if subset.empty:
                continue
            plt.plot(subset['queue_depth'], subset['bandwidth'], marker='o', label=f'{page_size}B')
        plt.title(f'Bandwidth vs Queue Depth ({op_type.capitalize()}, {method})')
        plt.xlabel('Queue Depth')
        plt.ylabel('Bandwidth (MB/s)')
        plt.xticks(queue_depths)
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend(title='Page Size')
        plt.savefig(f'bandwidth_{op_type}_{method}.png')
        plt.close()

for op_type in operations:
    for method in methods:
        plt.figure()
        for page_size in page_sizes:
            subset = df[(df['operation'] == op_type) & (df['method'] == method) & (df['page_size'] == page_size)]
            if subset.empty:
                continue
            plt.plot(subset['queue_depth'], subset['avg_latency'], marker='o', label=f'{page_size}B')
        plt.title(f'Average Latency vs Queue Depth ({op_type.capitalize()}, {method})')
        plt.xlabel('Queue Depth')
        plt.ylabel('Average Latency (us)')
        plt.xticks(queue_depths)
        plt.xscale('log', base=2)
        plt.grid(True)
        plt.legend(title='Page Size')
        plt.savefig(f'avg_latency_{op_type}_{method}.png')
        plt.close()


print("Benchmarking and plotting completed.")
