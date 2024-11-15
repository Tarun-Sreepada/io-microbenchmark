import subprocess
import re
import matplotlib.pyplot as plt
import itertools
import os
import pandas as pd
import seaborn as sns

# Define parameters
operations = ['read', 'write']
methods = ['seq', 'rand']
queue_depths = [1, 2, 8, 16, 4096, 16384]
page_sizes = [4096]
location = '/dev/nvme0n1'
io_operations = 50000
threads = 1

executables = {
    'async': os.path.join(os.path.dirname(os.path.realpath(__file__)), 'build/io_benchmark'),
    'sync': os.path.join(os.path.dirname(os.path.realpath(__file__)), 'build/io_benchmark_sync')
}

# Check for root privileges
if os.geteuid() != 0:
    print("This script needs to be run as root.")
    exit(1)

results = []

# Run benchmarks for each combination
for op, method, qd, page_size, (exec_type, exec_path) in itertools.product(operations, methods, queue_depths, page_sizes, executables.items()):
    print(f"Running benchmark: Exec={exec_type}, Type={op}, Method={method}, Queue Depth={qd}, Page Size={page_size}")
    
    cmd = [
        f'{exec_path}', f'--location={location}', f'--page_size={page_size}', f'--method={method}', 
        f'--type={op}', f'--io={io_operations}', f'--threads={threads}', f'--queue_depth={qd}', '-y'
    ]
    
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, universal_newlines=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running command: {e.output}")
        continue

    # Parse metrics from output
    metrics = {
        'total_io': int(re.search(r'Total I/O Completed:\s+(\d+)', output).group(1)) if re.search(r'Total I/O Completed:\s+(\d+)', output) else None,
        'total_time': float(re.search(r'Total Time:\s+([\d\.]+)\s+seconds', output).group(1)) if re.search(r'Total Time:\s+([\d\.]+)\s+seconds', output) else None,
        'iops': float(re.search(r'Throughput:\s+([\d\.]+(?:e\+[\d]+)?)\s+IOPS', output).group(1)) if re.search(r'Throughput:\s+([\d\.]+(?:e\+[\d]+)?)\s+IOPS', output) else None,
        'bandwidth': float(re.search(r'Throughput:\s+([\d\.]+)\s+MB/s', output).group(1)) if re.search(r'Throughput:\s+([\d\.]+)\s+MB/s', output) else None,
        'min_latency': float(re.search(r'Min Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output).group(1)) if re.search(r'Min Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output) else None,
        'max_latency': float(re.search(r'Max Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output).group(1)) if re.search(r'Max Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output) else None,
        'avg_latency': float(re.search(r'Average Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output).group(1)) if re.search(r'Average Latency:\s+([\d\.]+(?:e\+[\d]+)?)\s+microseconds', output) else None
    }

    if None in metrics.values():
        print("Failed to parse some metrics from output.")
        continue

    # Store results
    metrics.update({'execution': exec_type, 'operation': op, 'method': method, 'queue_depth': qd, 'page_size': page_size})
    results.append(metrics)

# Save results to CSV
df = pd.DataFrame(results)
df.to_csv('benchmark_results_sync_vs_async.csv', index=False)

# Set up plotting
sns.set(style="whitegrid")

for op in operations:
    for method in methods:
        for page_size in page_sizes:
            fig, axs = plt.subplots(3, 1, figsize=(10, 15))
            fig.suptitle(f'Metrics vs Queue Depth ({op.capitalize()}, {method}, Page Size={page_size})')
            metrics = [('IOPS', 'iops'), ('Bandwidth (MB/s)', 'bandwidth'), ('Average Latency (μs)', 'avg_latency')]
            
            for idx, (label, col) in enumerate(metrics):
                ax = axs[idx]
                subset_async = df[(df['execution'] == 'async') & (df['operation'] == op) & (df['method'] == method) & (df['page_size'] == page_size)]
                subset_sync = df[(df['execution'] == 'sync') & (df['operation'] == op) & (df['method'] == method) & (df['page_size'] == page_size)]
                
                if not subset_async.empty:
                    ax.plot(subset_async['queue_depth'], subset_async[col], marker='o', label='Async')
                if not subset_sync.empty:
                    ax.plot(subset_sync['queue_depth'], subset_sync[col], marker='x', label='Sync')
                
                ax.set_title(f'{label} vs Queue Depth')
                ax.set_xlabel('Queue Depth')
                ax.set_ylabel(label)
                ax.grid(True)
                ax.set_xscale('log', base=2)
                ax.legend()

                if not subset_sync.empty and not subset_async.empty:
                    ratio = subset_async[col].values / subset_sync[col].values
                    ax_ratio = ax.twinx()
                    ax_ratio.plot(subset_sync['queue_depth'], ratio, color='green', linestyle='dashed', marker='s', label='Sync/Async')
                    ax_ratio.set_ylabel(f'Sync/Async {label} Ratio')
                    ax_ratio.legend(loc='upper right')
                    # bring the legend down a bit
                    ax_ratio.legend(loc='upper right', bbox_to_anchor=(1.0, 0.9))

            plt.tight_layout(rect=[0, 0.03, 1, 1])
            plt.savefig(f'combined_metrics_{op}_{method}_{page_size}.png')
            plt.close(fig)

print("Benchmarking and plotting with combined metrics completed.")
