import subprocess
import matplotlib.pyplot as plt
import json
import time

# Parameters
operations = ['read', 'write']
methods = ['seq', 'rand']
queue_depths = [1, 2, 8, 32, 128, 256, 512]
page_sizes = [4096]
location = '/dev/nvme0n1'
threads = 4

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
        '--runtime=10',
        f'--numjobs={threads}',
        f'--filename={location}',
        '--time_based',
        '--output-format=json'
    ]
    print(f"Running FIO: {' '.join(cmd)}")
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
                    # Sum IOPS and bandwidth over all jobs
                    total_iops = sum(job[op]['iops'] for job in data['jobs'])
                    total_bandwidth = sum(job[op]['bw'] for job in data['jobs']) / 1024  # Convert to MB/s

                    # Initialize variables for latency calculation
                    total_iops_latency = 0
                    valid_jobs = 0  # Counter for jobs with valid latency

                    for job in data['jobs']:
                        job_iops = job[op]['iops']

                        # Access latency mean, checking which key exists
                        if 'clat' in job[op] and 'mean' in job[op]['clat']:
                            # 'clat' is in nanoseconds; convert to microseconds
                            latency_mean = job[op]['clat']['mean'] / 1000.0
                        elif 'lat_ns' in job[op] and 'mean' in job[op]['lat_ns']:
                            # 'lat_ns' is in nanoseconds; convert to microseconds
                            latency_mean = job[op]['lat_ns']['mean'] / 1000.0
                        elif 'lat_us' in job[op] and 'mean' in job[op]['lat_us']:
                            # 'lat_us' is already in microseconds
                            latency_mean = job[op]['lat_us']['mean']
                        elif 'lat' in job[op] and 'mean' in job[op]['lat']:
                            # 'lat' might be in microseconds
                            latency_mean = job[op]['lat']['mean']
                        else:
                            # If latency data is unavailable, skip this job
                            latency_mean = None

                        if latency_mean is not None:
                            total_iops_latency += job_iops * latency_mean
                            valid_jobs += 1

                    # Compute the weighted average latency
                    avg_latency = total_iops_latency / total_iops if total_iops > 0 else None

                    results[op][method][(qd, ps)] = {
                        'iops': total_iops,
                        'bandwidth': total_bandwidth,
                        'latency': avg_latency
                    }
                else:
                    results[op][method][(qd, ps)] = None  # In case of an error



# Plot the results and save them as a single image with three vertical subplots
fig, axes = plt.subplots(2, 1, figsize=(8, 16))

metrics = ['iops', 'bandwidth']
y_labels = ['IOPS', 'Bandwidth (MB/s)']


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
plt.savefig(f"fio_benchmark_results_{threads}_threads.png")
