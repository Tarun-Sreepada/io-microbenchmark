import subprocess
import re
import matplotlib.pyplot as plt
import os
import pandas as pd
import numpy as np
from brokenaxes import brokenaxes
import matplotlib

def run_benchmark(queue_depths, rw_types, duration, access_methods, thread_counts, engines, num_runs, csv_file):
    """
    Runs the io_benchmark command with different parameters and collects the results.

    Parameters:
        queue_depths (list): A list of queue depths to test.
        rw_types (list): A list containing 'read' and/or 'write' to specify the operation type.
        access_methods (list): A list containing 'rand' and/or 'seq' to specify access patterns.
        thread_counts (list): A list of thread counts to test.
        engines (list): A list of engines ('sync', 'liburing', 'io_uring') to test.
        num_runs (int): Number of times to run the benchmark for each parameter combination.
        duration (int): Duration of each benchmark run in seconds.
        csv_file (str): Path to the CSV file to store the results.
    """
    cur_dir = os.path.dirname(os.path.realpath(__file__))
    executable_location = os.path.join(cur_dir[:-7], 'build', 'io_benchmark')

    # Initialize CSV file if it doesn't exist
    if not os.path.exists(csv_file):
        df = pd.DataFrame(columns=['engine', 'rw', 'runtime', 'method', 'threads', 'queue_depth', 'run', 'iops', 'bandwidth'])
        df.to_csv(csv_file, index=False)
    else:
        df = pd.read_csv(csv_file)

    for engine in engines:
        for rw in rw_types:
            for method in access_methods:
                for threads in thread_counts:
                    run_queue_depths = [1] if engine == 'sync' else queue_depths  # For sync, test only one queue depth
                    for qd in run_queue_depths:
                        for run_num in range(num_runs):
                            config_exists = (
                                (df['engine'] == engine) &
                                (df['rw'] == rw) &
                                (df['runtime'] == duration) &
                                (df['method'] == method) &
                                (df['threads'] == threads) &
                                (df['queue_depth'] == qd) &
                                (df['run'] == run_num + 1)
                            ).any()
                            
                            if config_exists:
                                print(f"Skipping already completed configuration: Engine={engine}, RW={rw}, Method={method}, Threads={threads}, QD={qd}, Run={run_num+1}")
                                continue
                            
                            retries = 0
                            success = False
                            while retries < 3 and not success:
                                # sudo nvme flush /dev/nvme0n1
                                flush = subprocess.run(['sudo', 'nvme', 'reset', '/dev/nvme0'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                                print(flush.stdout)

                                cmd = [
                                    executable_location,
                                    '--location=/dev/nvme0n1',
                                    f'--engine={engine}',  # Use the engine flag
                                    f'--threads={threads}',
                                    f'--queue_depth={qd}',
                                    f'--method={method}',
                                    f'--type={rw}',
                                    '--time',
                                    f'--duration={duration}',
                                    '-y'
                                ]
                                print(f'Run {run_num+1}/{num_runs} - Running command:', ' '.join(cmd))
                                result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                                output = result.stdout + result.stderr
                                iops, bandwidth = parse_output("\n".join(output.split("\n")[-3:]))
                                if iops is not None and bandwidth is not None:
                                    # If engine is sync, apply the same result across all queue depths
                                    applicable_qd = queue_depths if engine == 'sync' else [qd]
                                    for depth in applicable_qd:
                                        row_df = pd.DataFrame({
                                            'engine': [engine],
                                            'rw': [rw],
                                            'runtime': [duration],
                                            'method': [method],
                                            'threads': [threads],
                                            'queue_depth': [depth],
                                            'run': [run_num+1],
                                            'iops': [iops],
                                            'bandwidth': [bandwidth]
                                        })
                                        row_df.to_csv(csv_file, mode='a', header=False, index=False)
                                    print(f'{engine} - Threads {threads}, QD {qd}, Run {run_num+1}: IOPS = {iops}, Bandwidth = {bandwidth} MB/s')
                                    success = True
                                else:
                                    retries += 1
                                    if retries < 3:
                                        print(f'Failed to parse output. Retrying ({retries}/3).')
                                    else:
                                        print(f'Failed after 3 retries. Skipping.')
                                        print(output)


def parse_output(output):
    """
    Parses the output from io_benchmark to extract IOPS and bandwidth using regular expressions.

    Parameters:
        output (str): The combined stdout and stderr output from io_benchmark.

    Returns:
        iops (float or None): The IOPS value extracted from the output.
        bandwidth (float or None): The bandwidth value extracted from the output.
    """
    # Updated regex to support scientific notation
    iops_match = re.search(r'Throughput:\s*([\d\.eE+-]+)\s*IOPS', output)
    bandwidth_match = re.search(r'Bandwidth:\s*([\d\.eE+-]+)\s*MB/s', output)
    
    if iops_match:
        iops = float(iops_match.group(1))
    else:
        iops = None
    
    if bandwidth_match:
        bandwidth = float(bandwidth_match.group(1))
    else:
        bandwidth = None
    
    return iops, bandwidth


def plot_threads_qd(csv_file, thread_counts, rw_types, access_methods, engines, queue_depths, duration):
    fig_size = (6, 6)

    df = pd.read_csv(csv_file)
    grouped = df.groupby(['engine', 'rw', 'runtime', 'method', 'threads', 'queue_depth']).mean().reset_index()

    for rw in rw_types:
        for method in access_methods:
            fig, ax_iops = plt.subplots(figsize=fig_size)
            fig_bandwidth, ax_bandwidth = plt.subplots(figsize=fig_size)

            # cmap1 = matplotlib.cm.get_cmap('tab20')
            cmap1 = plt.get_cmap('tab20')
            cmap2 = plt.get_cmap('tab20b')
            # cmap2 = matplotlib.cm.get_cmap('Set3')

            colors = [cmap1(i % 20) if i < 20 else cmap2(i % 12) for i in range(40)]
            
            color_idx = 0

            for engine in engines:
                for qd in queue_depths:
                    color = colors[color_idx]  # Assign color for the engine + queue depth combo
                    color_idx += 1

                    df_filtered = grouped[
                        (grouped['engine'] == engine) &
                        (grouped['rw'] == rw) &
                        (grouped['method'] == method) &
                        (grouped['queue_depth'] == qd)
                    ]

                    df_threads = df_filtered.set_index('threads').reindex(thread_counts)

                    threads_list = thread_counts
                    iops_list = df_threads['iops'].values
                    bandwidth_list = df_threads['bandwidth'].values

                    # Plot IOPS
                    ax_iops.plot(threads_list, iops_list, linestyle='-', marker='o',
                                 label=f"{engine} (QD={qd})", color=color)

                    # Plot Bandwidth
                    ax_bandwidth.plot(threads_list, bandwidth_list, linestyle='-', marker='o',
                                      label=f"{engine} (QD={qd})", color=color)

            # Set up titles, labels, and legends for IOPS
            ax_iops.set_title(f'IOPS - {rw.capitalize()} {method.capitalize()}')
            ax_iops.set_xlabel('Number of Threads')
            ax_iops.set_ylabel('IOPS')

            # Set up titles, labels, and legends for Bandwidth
            ax_bandwidth.set_title(f'Bandwidth - {rw.capitalize()} {method.capitalize()}')
            ax_bandwidth.set_xlabel('Number of Threads')
            ax_bandwidth.set_ylabel('Bandwidth (MB/s)')

            # create a svg for just the legend
            cur_dir = os.path.dirname(os.path.realpath(__file__))

            fig_legend = plt.figure(figsize=(10, 10))
            ax_legend = fig_legend.add_subplot(111)
            ax_legend.axis('off')
            ax_legend.legend(*ax_iops.get_legend_handles_labels(), loc='center', ncol=3)
            fig_legend.savefig(os.path.join(cur_dir, 'results', f'legend_{rw}_{method}.svg'), transparent=True)
            plt.close(fig_legend)

            # Save figures
            os.makedirs(os.path.join(cur_dir, 'results'), exist_ok=True)
            fig.savefig(os.path.join(cur_dir, 'results', f'iops_{rw}_{method}.svg'), transparent=True)
            fig_bandwidth.savefig(os.path.join(cur_dir, 'results', f'bandwidth_{rw}_{method}.svg'), transparent=True)

            plt.close(fig)
            plt.close(fig_bandwidth)



def plot_results(csv_file, queue_depths, rw_types, access_methods, thread_counts, engines, duration):
    fig_size = (10, 10)

    df = pd.read_csv(csv_file)
    grouped = df.groupby(['engine', 'rw', 'runtime',  'method', 'threads', 'queue_depth']).mean().reset_index()

    max_offset = 0.01  # Maximum offset as a fraction of the data point value

    for threads in thread_counts:
        fig, axes = plt.subplots(len(rw_types), len(access_methods), figsize=fig_size, squeeze=False)
        fig_bandwidth, axes_bandwidth = plt.subplots(len(rw_types), len(access_methods), figsize=fig_size, squeeze=False)

        for i, rw in enumerate(rw_types):
            for j, method in enumerate(access_methods):
                ax = axes[i, j]
                ax_bandwidth = axes_bandwidth[i, j]

                cmap = plt.get_cmap('tab20')

                colors = [cmap(k) for k in range(len(engines))]  # Generate colors

                for engine_idx, engine in enumerate(engines):
                    color = colors[engine_idx]  # Assign color for the engine
                    df_filtered = grouped[
                        (grouped['engine'] == engine) &
                        (grouped['rw'] == rw) &
                        (grouped['method'] == method) &
                        (grouped['threads'] == threads) &
                        (grouped['runtime'] == duration)
                    ]

                    df_qd = df_filtered.set_index('queue_depth').reindex(queue_depths)

                    qd_list = queue_depths
                    iops_list = df_qd['iops'].values
                    bandwidth_list = df_qd['bandwidth'].values

                    ax.plot(qd_list, iops_list, linestyle='-', marker='o', label=engine, color=color)
                    ax_bandwidth.plot(qd_list, bandwidth_list, linestyle='-', marker='o', label=engine, color=color)

                    # # Annotate improvement factors for IOPS
                    # for idx in range(1, len(qd_list)):
                    #     if iops_list[idx - 1] > 0:  # Avoid division by zero
                    #         improvement_factor_iops = iops_list[idx] / iops_list[idx - 1]
                    #         offset = min(max_offset * iops_list[idx], 10)  # Constrain the offset
                    #         ax.annotate(
                    #             f"{improvement_factor_iops:.3f}x",
                    #             xy=(qd_list[idx], iops_list[idx]),
                    #             xytext=(qd_list[idx], iops_list[idx] + offset),
                    #             fontsize=10,
                    #             color=color,  # Use the same color
                    #             ha='center'
                    #         )

                    # # Annotate improvement factors for Bandwidth
                    # for idx in range(1, len(qd_list)):
                    #     if bandwidth_list[idx - 1] > 0:  # Avoid division by zero
                    #         improvement_factor_bandwidth = bandwidth_list[idx] / bandwidth_list[idx - 1]
                    #         offset = min(max_offset * bandwidth_list[idx], 10)  # Constrain the offset
                    #         ax_bandwidth.annotate(
                    #             f"{improvement_factor_bandwidth:.3f}x",
                    #             xy=(qd_list[idx], bandwidth_list[idx]),
                    #             xytext=(qd_list[idx], bandwidth_list[idx] + offset),
                    #             fontsize=10,
                    #             color=color,  # Use the same color
                    #             ha='center'
                    #         )

                ax.set_title(f'IOPS - {rw.capitalize()} {method.capitalize()}')
                ax.set_xlabel('Queue Depth')
                ax.set_ylabel('IOPS')
                ax.set_xscale('log', base=2)
                ax.legend()

                ax_bandwidth.set_xscale('log', base=2)
                ax_bandwidth.set_title(f'Bandwidth - {rw.capitalize()} {method.capitalize()}')
                ax_bandwidth.set_xlabel('Queue Depth')
                ax_bandwidth.set_ylabel('Bandwidth (MB/s)')
                ax_bandwidth.legend()

        cur_dir = os.path.dirname(os.path.realpath(__file__))
        os.makedirs(os.path.join(cur_dir, 'results'), exist_ok=True)
        fig.savefig(os.path.join(cur_dir, 'results', f'iops_threads_{threads}.svg'), transparent=True)
        fig_bandwidth.savefig(os.path.join(cur_dir, 'results', f'bandwidth_threads_{threads}.svg'), transparent=True)

        plt.close(fig)
        plt.close(fig_bandwidth)


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
    queue_depths = [1, 2, 4, 32, 64, 128, 512]
    rw_types = ['read', 'write']
    access_methods = ['rand', 'seq']
    thread_counts = [1,2,4]
    engines = ['sync', 'liburing', 'io_uring']
    num_runs = 1
    duration = 5

    # if results folder does not exist where the script is located, create it
    if not os.path.exists(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'results')):
        os.makedirs(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'results'))

    cur_dir = os.path.dirname(os.path.realpath(__file__))
    csv_file = os.path.join(cur_dir, 'results', 'benchmark_results.csv')

    run_benchmark(queue_depths, rw_types, duration, access_methods, thread_counts, engines, num_runs, csv_file)

    # Plot results
    # plot_threads_qd(csv_file, thread_counts, rw_types, access_methods, engines, queue_depths)
    plot_results(csv_file, queue_depths, rw_types, access_methods, thread_counts, engines, duration)


if __name__ == '__main__':
    main()
