import subprocess
import matplotlib.pyplot as plt
import json
import time

# Parameters
operations = ['read', 'write']
methods = ['seq', 'rand']
queue_depths = [1, 2, 8, 16, 4096]
page_sizes = [4096]
location = '/dev/nvme0n1'
threads = 1

# Mapping for FIO command operations
fio_operations = {
    ('read', 'seq'): 'read',
    ('read', 'rand'): 'randread',
    ('write', 'seq'): 'write',
    ('write', 'rand'): 'randwrite'
}

# Function to run FIO and get results
def run_fio(operation, method, queue_depth, page_size):
    fio_op = fio_operations[(operation, method)]
    cmd = [
        'fio',
        '--name=test',
        f'--rw={fio_op}',
        f'--bs={page_size}',
        f'--ioengine=io_uring',
        f'--iodepth={queue_depth}',
        '--size=1G',
        '--runtime=3',
        f'--numjobs={threads}',
        f'--filename={location}',
        '--time_based',
        '--output-format=json'
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error running FIO: {result.stderr}")
        return None
    
    try:
        data = json.loads(result.stdout)
        return data
    except json.JSONDecodeError as e:
        print(f"JSON decoding error: {e}")
        print(f"Output was: {result.stdout}")
        return None

# Store results in a dictionary
results = {op: {method: {} for method in methods} for op in operations}

# Collect results for each combination
for op in operations:
    for method in methods:
        for qd in queue_depths:
            for ps in page_sizes:
                print(f"Running FIO benchmark: Operation={op}, Method={method}, Queue Depth={qd}, Page Size={ps}")
                start = time.time()
                data = run_fio(op, method, qd, ps)
                end = time.time()
                estimated_avg = (end - start) / 3
                if data is not None:
                    # Extract metrics
                    iops = data['jobs'][0][op]['iops']
                    bandwidth = data['jobs'][0][op]['bw'] / 1024  # Bandwidth in KB/s, convert to MB/s
                    avg_latency = data['jobs'][0][op].get('lat', {}).get('mean', None)  # Average latency in usec, or None if not available
                    results[op][method][(qd, ps)] = {'iops': iops, 'bandwidth': bandwidth, 'latency': avg_latency}
                else:
                    results[op][method][(qd, ps)] = None  # In case of an error

# Plot the results and save them as a single image with three vertical subplots
fig, axes = plt.subplots(3, 1, figsize=(8, 18))

metrics = ['iops', 'bandwidth', 'latency']
y_labels = ['IOPS', 'Bandwidth (MB/s)', 'Average Latency (usec)']


for i, metric in enumerate(metrics):
    for op in operations:
        for method in methods:
            metric_values = [
                results[op][method][(qd, page_sizes[0])][metric] 
                for qd in queue_depths 
                if results[op][method][(qd, page_sizes[0])] is not None and results[op][method][(qd, page_sizes[0])][metric] is not None
            ]
            queue_depths_filtered = [
                qd for qd in queue_depths 
                if results[op][method][(qd, page_sizes[0])] is not None and results[op][method][(qd, page_sizes[0])][metric] is not None
            ]
            
            if metric_values:  # Only plot if there are valid values
                axes[i].plot(queue_depths_filtered, metric_values, marker='o', label=f'{fio_operations[(op, method)]}')
                
    axes[i].set_xlabel('Queue Depth')
    axes[i].set_ylabel(y_labels[i])
    axes[i].set_title(f'FIO {y_labels[i]} Benchmark Results on {location}')
    axes[i].legend()
    axes[i].grid(True)
    axes[i].set_xscale('log', base=2)

# Save the figure
plt.tight_layout()
plt.savefig("fio_benchmark_results.png")
