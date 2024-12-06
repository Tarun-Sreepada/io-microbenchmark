import os
import subprocess
import time
import pandas as pd
import re
import matplotlib.pyplot as plt
import argparse

# Configuration
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "monitoring_logs")
cur_dir = os.path.dirname(os.path.realpath(__file__))
executable_location = os.path.join(cur_dir[:-7], 'build', 'io_benchmark')

# Function to run a command and log its output
def run_command(command, log_file):
    with open(log_file, "w") as f:
        process = subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT)
    return process

# Functions for log parsing
def read_sar_log(file_path):
    sar_lines = []
    with open(file_path, 'r') as file:
        file.readline()  # Skip the first line
        for line in file:
            line = line.strip()
            if not line:
                continue
            line = re.sub(' +', ' ', line)
            sar_lines.append(line.split())
    sar_df = pd.DataFrame(sar_lines)
    sar_df.drop([0, 1, 2], axis=1, inplace=True)
    sar_df.columns = sar_df.iloc[0]
    sar_df.drop(index=0, inplace=True)
    sar_df = sar_df.apply(pd.to_numeric, errors='coerce')
    sar_df.reset_index(drop=True, inplace=True)
    # drop %idle
    sar_df.drop(columns="%idle", inplace=True)
    
    return sar_df

def read_iostat_log(file_path, plot=[]):
    iostat_lines = []
    with open(file_path, 'r') as file:
        file.readline()
        for line in file:
            line = line.strip()
            if not line:
                continue
            line = re.sub(' +', ' ', line)
            iostat_lines.append(line.split())
    iostat_lines = [line for line in iostat_lines if not re.match(r".*AM|PM.*", line[2])]
    header = iostat_lines[0]
    iostat_lines = [line for line in iostat_lines if line != header]
    iostat_df = pd.DataFrame(iostat_lines, columns=header)
    iostat_df.drop(columns="Device", inplace=True)
    iostat_df = iostat_df.apply(pd.to_numeric, errors='coerce')
    columns = iostat_df.columns.intersection(plot)
    return iostat_df[columns]

def read_benchmark_log(file_path):
    lines = []
    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if re.match(r".*Thread 0:.*", line):
                lines.append(line)
    data = re.findall(r"Elapsed Time: ([\d.]+)s, IOPS: ([\d.eE+-]+), Bandwidth: ([\d.]+) MB/s", " ".join(lines))
    results = pd.DataFrame(data, columns=["Elapsed Time (s)", "IOPS", "Bandwidth (MB/s)"])
    results = results.apply(pd.to_numeric, errors='coerce')
    results["Bandwidth (MB/s)"] *= 1024  # Convert to KB/s
    results.rename(columns={"Bandwidth (MB/s)": "Bandwidth (KB/s)"}, inplace=True)
    results.set_index("Elapsed Time (s)", inplace=True)
    return results

# Plotting function
def plot_logs(io_df, sar_df, benchmark_df, method, type):
    # set figure size
    plt.figure(figsize=(8, 12))
    
    # offset the df by 3 seconds
    io_df.index = io_df.index - 2
    sar_df.index = sar_df.index - 2
    
    # take iodf and benchmark df convert ot MB/s
    # io_df["rMB/s"] = io_df["rkB/s"] / 1024
    if type == "read":
        io_df["rMB/s"] = io_df["rkB/s"] / 1024
    else:
        io_df["wMB/s"] = io_df["wkB/s"] / 1024
    
    benchmark_df["Bandwidth (MB/s)"] = benchmark_df["Bandwidth (KB/s)"] / 1024
    
    
    fig, axes = plt.subplots(3, 1, figsize=(12, 16), sharex=True)

    axes[0].set_title("Bandwidth (MB/s)")
    # axes[0].plot(io_df.index, io_df["rMB/s"], label="iostat rMB/s", color="blue", linewidth=2)
    if type == "read":
        axes[0].plot(io_df.index, io_df["rMB/s"], label="iostat rMB/s", color="blue", linewidth=2)
    else:
        axes[0].plot(io_df.index, io_df["wMB/s"], label="iostat wMB/s", color="blue", linewidth=2)
    axes[0].plot(benchmark_df.index, benchmark_df["Bandwidth (MB/s)"], label="Benchmark Bandwidth", color="green", linewidth=2)
    axes[0].set_ylabel("Bandwidth (MB/s)")
    axes[0].legend()
    axes[0].grid(True)
    

    # Middle plot: IOPS (Benchmark and Iostat r/s)
    axes[1].set_title("IOPS (Operations per Second)")
    # axes[1].plot(io_df.index, io_df["r/s"], label="iostat r/s", color="blue", linewidth=2)
    if type == "read":
        axes[1].plot(io_df.index, io_df["r/s"], label="iostat r/s", color="blue", linewidth=2)
    else:
        axes[1].plot(io_df.index, io_df["w/s"], label="iostat w/s", color="blue", linewidth=2)
    axes[1].plot(benchmark_df.index, benchmark_df["IOPS"], label="Benchmark IOPS", color="orange", linewidth=2)
    axes[1].set_ylabel("IOPS")
    axes[1].legend()
    axes[1].grid(True)

    # Bottom plot: CPU usage
    axes[2].set_title("CPU Usage (SAR)")
    cumulative_bottom = None
    for column in sar_df.columns:
        if cumulative_bottom is None:
            axes[2].bar(sar_df.index, sar_df[column], label=f"CPU: {column}", alpha=0.3)
            cumulative_bottom = sar_df[column].copy()
        else:
            axes[2].bar(sar_df.index, sar_df[column], label=f"CPU: {column}", alpha=0.3, bottom=cumulative_bottom)
            cumulative_bottom += sar_df[column]
    axes[2].set_xlabel("Time (index)")
    axes[2].set_ylabel("CPU Usage (%)")
    axes[2].legend()
    axes[2].grid(True)

    plt.tight_layout()
    plt.savefig(os.path.join(OUTPUT_DIR, f"io_cpu_usage_{method}_{type}.svg"), transparent=True)
    plt.close()

# Main script execution
def main():
    parser = argparse.ArgumentParser(description="I/O Benchmark and Monitoring")
    parser.add_argument("--device", type=str, default="nvme0n1", help="Device to monitor")
    parser.add_argument("--method", type=str, choices=["rand", "seq"], default="rand", help="Benchmark method (rand/seq)")
    parser.add_argument("--type", type=str, choices=["read", "write"], default="read", help="Benchmark type (read/write)")
    parser.add_argument("--page_size", type=int, default=4096, help="Page size in bytes")
    parser.add_argument("--queue_depth", type=int, default=1, help="Queue depth")
    parser.add_argument("--duration", type=int, default=15, help="Benchmark duration in seconds")
    args = parser.parse_args()

    device = args.device
    method = args.method
    b_type = args.type
    page_size = args.page_size
    queue_depth = args.queue_depth
    MONITOR_DURATION = args.duration + 5  # 3 seconds before + buffer
    DURATION = args.duration
    
    # MONITOR_DURATION = DURATION + 5  # 3 seconds before + buffer
    

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    iostat_log = os.path.join(OUTPUT_DIR, f"iostat_{device}.log")
    sar_log = os.path.join(OUTPUT_DIR, "sar_cpu0.log")
    benchmark_output = os.path.join(OUTPUT_DIR, "benchmark_output.log")

    # Start iostat and sar
    iostat_process = run_command(["iostat", "-t", "-dx", device, "1", str(MONITOR_DURATION)], iostat_log)
    sar_process = run_command(["sar", "-P", "0", "1", str(MONITOR_DURATION)], sar_log)

    print("Waiting 2 seconds before starting the benchmark...")
    time.sleep(2)

    # Run benchmark
    benchmark_cmd = [
        executable_location,
        "--engine=io_uring",
        f"--location=/dev/{device}",
        "--time",
        f"--duration={DURATION}",
        f"--page_size={page_size}",
        f"--method={method}",
        f"--type={b_type}",
        f"--queue_depth={queue_depth}",
        "-y"
    ]
    print(f"Running benchmark with command: {' '.join(benchmark_cmd)}")
    with open(benchmark_output, "w") as f:
        subprocess.run(benchmark_cmd, stdout=f, stderr=subprocess.STDOUT)

    # Wait for monitoring processes
    iostat_process.wait()
    sar_process.wait()

    print(f"Monitoring completed. Logs are saved in {OUTPUT_DIR}.")

    # Process logs and plot
    # io_df = read_iostat_log(iostat_log, plot=["r/s", "rkB/s"])
    if b_type == "read":
        io_df = read_iostat_log(iostat_log, plot=["r/s", "rkB/s"])
    else:
        io_df = read_iostat_log(iostat_log, plot=["w/s", "wkB/s"])
    sar_df = read_sar_log(sar_log)
    benchmark_df = read_benchmark_log(benchmark_output)
    
    plot_logs(io_df, sar_df, benchmark_df, method, b_type)

if __name__ == "__main__":
    main()
