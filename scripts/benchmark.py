import subprocess
import re
import matplotlib.pyplot as plt
import os
import pandas as pd
import numpy as np

def run_benchmark(queue_depths, rw_types, access_methods, thread_counts, num_runs, duration, csv_file):
    """
    Runs the io_benchmark command with different parameters and collects the results.

    Parameters:
        queue_depths (list): A list of queue depths to test.
        rw_types (list): A list containing 'read' and/or 'write' to specify the operation type.
        access_methods (list): A list containing 'rand' and/or 'seq' to specify access patterns.
        thread_counts (list): A list of thread counts to test.
        num_runs (int): Number of times to run the benchmark for each parameter combination.
        duration (int): Duration of each benchmark run in seconds.
    """
    cur_dir = os.path.dirname(os.path.realpath(__file__))
    # remove the scripts folder from the path and add build folder
    executable_location = os.path.join(cur_dir[:-7], 'build', 'io_benchmark')

    # Initialize CSV file if it doesn't exist
    if not os.path.exists(csv_file):
        df = pd.DataFrame(columns=['rw', 'method', 'threads', 'queue_depth', 'run', 'iops', 'bandwidth'])
        df.to_csv(csv_file, index=False)
    else:
        df = pd.read_csv(csv_file)

    for rw in rw_types:
        for method in access_methods:
            for threads in thread_counts:
                for qd in queue_depths:
                    for run_num in range(num_runs):
                        retries = 0
                        success = False
                        while retries < 3 and not success:
                            cmd = [
                                executable_location,
                                '--location=/dev/nvme0n1',
                                '--async',
                                f'--threads={threads}',
                                f'--queue_depth={qd}',
                                f'--method={method}',
                                f'--type={rw}',
                                '--time',
                                f'--duration={duration}',
                                '-y'
                            ]
                            print(f'Run {run_num+1}/{num_runs} - Running command:', ' '.join(cmd))
                            result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=duration*2)
                            output = result.stdout + result.stderr  # Combine stdout and stderr
                            output = "\n".join(output.split("\n")[-8:])
                            print(output)
                            iops, bandwidth = parse_output(output)
                            if iops is not None and bandwidth is not None:
                                # Create a DataFrame with one row
                                row_df = pd.DataFrame({
                                    'rw': [rw],
                                    'method': [method],
                                    'threads': [threads],
                                    'queue_depth': [qd],
                                    'run': [run_num+1],
                                    'iops': [iops],
                                    'bandwidth': [bandwidth]
                                })
                                # Append to CSV
                                row_df.to_csv(csv_file, mode='a', header=False, index=False)
                                print(f'Threads {threads}, Queue depth {qd}, Run {run_num+1}: IOPS = {iops}, Bandwidth = {bandwidth} MB/s')
                                success = True
                            else:
                                retries += 1
                                if retries < 3:
                                    print(f'Failed to parse output for threads {threads}, queue depth {qd}, run {run_num+1}, retrying ({retries}/3)')
                                else:
                                    print(f'Failed to parse output for threads {threads}, queue depth {qd}, run {run_num+1}, after 3 retries. Skipping.')


def parse_output(output):
    """
    Parses the output from io_benchmark to extract IOPS and bandwidth using regular expressions.

    Parameters:
        output (str): The combined stdout and stderr output from io_benchmark.

    Returns:
        iops (float or None): The IOPS value extracted from the output.
        bandwidth (float or None): The bandwidth value extracted from the output.
    """
    iops_match = re.search(r'Throughput:\s*([\d\.]+)\s*IOPS', output)
    bandwidth_match = re.search(r'Bandwidth:\s*([\d\.]+)\s*MB/s', output)
    if iops_match and bandwidth_match:
        iops = float(iops_match.group(1))
        bandwidth = float(bandwidth_match.group(1))
        return iops, bandwidth
    else:
        return None, None


def plot_results(csv_file, queue_depths, rw_types, access_methods, thread_counts):
    """
    Plots the IOPS and bandwidth results against queue depths, comparing different thread counts.
    If a data point doesn't exist, it interpolates to estimate it and represents it with a dotted line.

    Parameters:
        csv_file (str): The path to the CSV file containing the benchmark results.
        queue_depths (list): The list of queue depths tested.
        rw_types (list): The read/write types used in the benchmark.
        access_methods (list): The access methods used in the benchmark.
        thread_counts (list): The thread counts used in the benchmark.
    """
    # Read the data from CSV
    df = pd.read_csv(csv_file)

    # Compute averages
    # Group by ['rw', 'method', 'threads', 'queue_depth'] and compute mean of 'iops' and 'bandwidth'
    grouped = df.groupby(['rw', 'method', 'threads', 'queue_depth']).mean().reset_index()

    # Set up subplots
    num_rw = len(rw_types)
    num_methods = len(access_methods)
    fig, axes = plt.subplots(num_rw, num_methods, figsize=(15, 10))
    fig_bandwidth, axes_bandwidth = plt.subplots(num_rw, num_methods, figsize=(15, 10))

    # Ensure axes are 2D arrays
    if num_rw == 1:
        axes = np.array([axes])
        axes_bandwidth = np.array([axes_bandwidth])
    if num_methods == 1:
        axes = axes.reshape(-1, 1)
        axes_bandwidth = axes_bandwidth.reshape(-1, 1)

    for i, rw in enumerate(rw_types):
        for j, method in enumerate(access_methods):
            ax = axes[i, j]
            ax_bandwidth = axes_bandwidth[i, j]

            for threads in thread_counts:
                # Filter data for this combination
                df_filtered = grouped[
                    (grouped['rw'] == rw) &
                    (grouped['method'] == method) &
                    (grouped['threads'] == threads)
                ]

                # Create a DataFrame indexed by queue_depth
                df_qd = df_filtered.set_index('queue_depth').reindex(queue_depths)

                # Keep track of missing data before interpolation
                missing_iops = df_qd['iops'].isna()
                missing_bandwidth = df_qd['bandwidth'].isna()

                # Interpolate missing values
                df_qd['iops'] = df_qd['iops'].interpolate(method='linear')
                df_qd['bandwidth'] = df_qd['bandwidth'].interpolate(method='linear')

                # Fill any remaining NaNs at the ends
                df_qd['iops'] = df_qd['iops'].fillna(method='bfill').fillna(method='ffill')
                df_qd['bandwidth'] = df_qd['bandwidth'].fillna(method='bfill').fillna(method='ffill')

                # Get the data for plotting
                iops_list = df_qd['iops'].values
                bandwidth_list = df_qd['bandwidth'].values
                qd_list = queue_depths

                # Identify interpolated points
                interpolated_iops = missing_iops.values
                interpolated_bandwidth = missing_bandwidth.values

                # Plot IOPS
                # Plot actual data with solid lines
                ax.plot(qd_list, iops_list, linestyle='-', marker='o', label=f'{threads} Threads')

                # Overlay interpolated points with dotted lines
                for idx in range(len(qd_list)-1):
                    x_vals = qd_list[idx:idx+2]
                    y_vals = iops_list[idx:idx+2]
                    if interpolated_iops[idx] or interpolated_iops[idx+1]:
                        ax.plot(x_vals, y_vals, linestyle='--', color=ax.lines[-1].get_color())

                # Plot Bandwidth
                # Plot actual data with solid lines
                ax_bandwidth.plot(qd_list, bandwidth_list, linestyle='-', marker='o', label=f'{threads} Threads')

                # Overlay interpolated points with dotted lines
                for idx in range(len(qd_list)-1):
                    x_vals = qd_list[idx:idx+2]
                    y_vals = bandwidth_list[idx:idx+2]
                    if interpolated_bandwidth[idx] or interpolated_bandwidth[idx+1]:
                        ax_bandwidth.plot(x_vals, y_vals, linestyle='--', color=ax_bandwidth.lines[-1].get_color())

            ax.set_title(f'IOPS - {rw.capitalize()} {method.capitalize()}')
            ax.set_xlabel('Queue Depth')
            ax.set_ylabel('IOPS')
            ax.set_xscale('log', base=2)
            ax.grid(True)
            ax.legend()

            ax_bandwidth.set_title(f'Bandwidth - {rw.capitalize()} {method.capitalize()}')
            ax_bandwidth.set_xlabel('Queue Depth')
            ax_bandwidth.set_ylabel('Bandwidth (MB/s)')
            ax_bandwidth.set_xscale('log', base=2)
            ax_bandwidth.grid(True)
            ax_bandwidth.legend()

    # save in results folder
    cur_dir = os.path.dirname(os.path.realpath(__file__))
    fig.savefig(os.path.join(cur_dir, 'results', 'combined_iops.png'))
    fig_bandwidth.savefig(os.path.join(cur_dir, 'results', 'combined_bandwidth.png'))


def test():

    # run write seq with 2 threads and print the captured output
    cur_dir = os.path.dirname(os.path.realpath(__file__))
    # remove the scripts folder from the path and add build folder
    executable_location = os.path.join(cur_dir[:-7], 'build', 'io_benchmark')
    cmd = [
        executable_location,
        '--location=/dev/nvme0n1',
        '--async',
        '--threads=1',
        '--queue_depth=256',
        '--method=seq',
        '--type=read',
        '--time',
        '--duration=20',
        '-y'
    ]
    print(f'Running command:', ' '.join(cmd))
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20*2)
    output = result.stdout + result.stderr  # Combine stdout and stderr
    
    print("\n".join(output.split("\n")[-8:]))


def main():
    """
    The main function that orchestrates the benchmark execution and plotting.
    """
    # Define the parameters to test
    queue_depths = [1, 2, 4, 16, 64, 256]
    rw_types = ['read', 'write']
    access_methods = ['rand', 'seq']
    thread_counts = [1, 2, 4]
    num_runs = 3  # Number of times to run each benchmark and average results
    duration = 20

    # save the csv file in the same directory as the script but in results folder
    cur_dir = os.path.dirname(os.path.realpath(__file__))
    csv_file = os.path.join(cur_dir, 'results', 'benchmark_results.csv')


    # Check if CSV file exists
    if not os.path.exists(csv_file):
        # Run benchmarks
        run_benchmark(queue_depths, rw_types, access_methods, thread_counts, num_runs, duration, csv_file)
    else:
        print(f'{csv_file} exists. Skipping benchmark execution and plotting existing data.')

    # Plot results
    plot_results(csv_file, queue_depths, rw_types, access_methods, thread_counts)



if __name__ == '__main__':
    main()
