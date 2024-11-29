import matplotlib.pyplot as plt
import pandas as pd
import re
import os
import pandas as pd

# Configuration
# files are in the monitoring_logs directory which is in the same directory as this script
monitoring_logs_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)), "monitoring_logs")

# sar_log = "./monitoring_logs/sar_cpu0.log"  # Path to the sar log file
sar_log = os.path.join(monitoring_logs_dir, "sar_cpu0.log")  # Path to the sar log file
# iostat_log = "./monitoring_logs/iostat_nvme0n1.log"  # Path to the iostat log file
iostat_log = os.path.join(monitoring_logs_dir, "iostat_nvme0n1.log")  # Path to the iostat log file

benchmark_log = os.path.join(monitoring_logs_dir, "benchmark_output.log")  # Path to the benchmark log file

def read_sar_log(file_path, columns_to_drop=None, title="CPU Usage Statistics Over Time"):
    """
    Reads and plots a SAR log file.

    Parameters:
    - file_path (str): Path to the SAR log file.
    - columns_to_drop (list): Columns to drop from the DataFrame. Default is None.
    - title (str): Title for the plot. Default is "CPU Usage Statistics Over Time".

    Returns:
    - None: Displays the plot.
    """
    sar_lines = []

    # Read and preprocess the file
    with open(file_path, 'r') as file:
        file.readline()  # Skip the first line (assumed to be metadata)
        for line in file:
            line = line.strip()
            if not line:
                continue
            # Remove repeated spaces and split into columns
            line = re.sub(' +', ' ', line)
            sar_lines.append(line.split())

    # Create a DataFrame
    sar_df = pd.DataFrame(sar_lines)
    
    # Drop the first three columns
    sar_df.drop([0, 1, 2], axis=1, inplace=True)
    
    # Use the first row as column headers
    sar_df.columns = sar_df.iloc[0]
    sar_df.drop(index=0, inplace=True)

    # Convert data to numeric
    sar_df = sar_df.apply(pd.to_numeric, errors='coerce')

    # reset the index
    sar_df.reset_index(drop=True, inplace=True)


    # find the time before idle reduces 
    time_before_idle_reduces = sar_df[sar_df['%idle'] < 50].index[0] - 1
    sar_df = sar_df.loc[time_before_idle_reduces:]


    for column in columns_to_drop:
        if column in sar_df.columns:
            sar_df.drop(columns=column, inplace=True)


    return sar_df


def read_iostat_log(file_path, title="NVMe Device Metrics Over Time", plot=[]):
    """
    Reads and plots an iostat log file.

    Parameters:
    - file_path (str): Path to the iostat log file.
    - title (str): Title for the plot. Default is "NVMe Device Metrics Over Time".

    Returns:
    - None: Displays the plot.
    """
    iostat_lines = []

    # Read and preprocess the file
    with open(file_path, 'r') as file:
        file.readline()
        for line in file:
            line = line.strip()
            if not line:
                continue
            # Remove repeated spaces and split into columns
            line = re.sub(' +', ' ', line)
            iostat_lines.append(line.split())

    # remove lines that contain time like AM or PM in 3rd column
    iostat_lines = [line for line in iostat_lines if not re.match(r".*AM|PM.*", line[2])]

    # remove all duplicates of the first row
    header = iostat_lines[0]
    iostat_lines = [line for line in iostat_lines if line != header]

    # Create a DataFrame
    iostat_df = pd.DataFrame(iostat_lines, columns=header)
    # drop the Device column
    iostat_df.drop(columns="Device", inplace=True)
    # convert the data to numeric
    iostat_df = iostat_df.apply(pd.to_numeric, errors='coerce')

    # only keep the specific columns to plot
    columns = iostat_df.columns.intersection(plot)
    iostat_df = iostat_df[columns]

    return iostat_df

def plot_logs_dual_axis(io_df, sar_df, benchmark_df, io_shift=0, sar_shift=0, benchmark_shift=0):
    """
    Plots I/O metrics on the left y-axis and CPU metrics on the right y-axis.

    Parameters:
    - io_df (DataFrame): DataFrame containing I/O metrics.
    - sar_df (DataFrame): DataFrame containing CPU metrics.
    - io_shift (int): Number of steps to shift the I/O metrics DataFrame. Positive values shift right, negative shift left.
    - sar_shift (int): Number of steps to shift the CPU metrics DataFrame. Positive values shift right, negative shift left.

    Returns:
    - None: Displays the plot.
    """
    # Shift the DataFrames
    io_df = io_df.shift(io_shift)
    sar_df = sar_df.shift(sar_shift)
    benchmark_df = benchmark_df.shift(benchmark_shift)

    fig, ax1 = plt.subplots(figsize=(12, 8))

    # Plot I/O metrics on the left y-axis
    ax1.set_xlabel("Time (index)")
    ax1.set_ylabel("I/O Metrics", color="blue")
    for column in io_df.columns:
        ax1.plot(io_df.index, io_df[column], label=f"I/O: {column}", linewidth=5)
    ax1.tick_params(axis="y", labelcolor="blue")
    ax1.legend(loc="upper left", bbox_to_anchor=(0.05, 1))

    for column in benchmark_df.columns:
        ax1.plot(benchmark_df.index, benchmark_df[column], label=f"Benchmark: {column}", linewidth=5)
    ax1.legend(loc="upper left", bbox_to_anchor=(0.05, 1))


    # Create a second y-axis for CPU metrics
    ax2 = ax1.twinx()
    ax2.set_ylabel("CPU Usage (%)", color="orange")
    for column in sar_df.columns:
        ax2.bar(sar_df.index, sar_df[column], label=f"CPU: {column}", alpha=0.3)
    ax2.tick_params(axis="y", labelcolor="orange")
    ax2.legend(loc="upper right", bbox_to_anchor=(0.95, 1))

    # Set the title
    plt.title("I/O and CPU Usage Statistics Over Time")
    plt.grid(True)
    plt.tight_layout()

    # Save the plot to the monitoring_logs directory
    plt.savefig(os.path.join(monitoring_logs_dir, "io_cpu_usage.png"))
    # plt.show()


def read_benchmark_log(file_path):

    lines = []

    with open (file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            # if line contains Thread 0:
            if re.match(r".*Thread 0:.*", line):
                lines.append(line)

    data = []
    pattern = r"Elapsed Time: ([\d.]+)s, IOPS: ([\d.eE+-]+), Bandwidth: ([\d.]+) MB/s"

    data = re.findall(pattern, " ".join(lines))

    results = pd.DataFrame(data, columns=["Elapsed Time (s)", "IOPS", "Bandwidth (MB/s)"])
    results = results.apply(pd.to_numeric, errors='coerce')

    # Bandwidth is in MB/s, convert to KB/s
    results["Bandwidth (MB/s)"] = results["Bandwidth (MB/s)"] * 1024
    # rename the columns
    results.rename(columns={"Bandwidth (MB/s)": "Bandwidth (KB/s)"}, inplace=True)
 


    # make Elapsed Time the index
    results.set_index("Elapsed Time (s)", inplace=True)

    # print(results)
    return results

        


io_df = read_iostat_log(iostat_log, title="NVMe Device Metrics Over Time", plot=['r/s', 'rkB/s'])
sar_df = read_sar_log(sar_log, columns_to_drop=['%nice', '%steal', '%system'], title="CPU Usage Statistics Over Time")
benchmark_df = read_benchmark_log(benchmark_log)

plot_logs_dual_axis(io_df, sar_df, benchmark_df, -1, 0, 3)
