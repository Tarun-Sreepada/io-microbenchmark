import platform
import psutil
import subprocess

def get_cpu_info():
    cpu_info = {
        "CPU Cores": psutil.cpu_count(logical=False),
        "CPU Threads": psutil.cpu_count(logical=True),
        "CPU Name": platform.processor()
    }
    
    if platform.system() == "Linux":
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if "model name" in line:
                        cpu_info["CPU Name"] = line.strip().split(":")[1].strip()
                        break
        except FileNotFoundError:
            pass
    
    return cpu_info

def get_memory_info():
    virtual_mem = psutil.virtual_memory()
    mem_info = {
        "DRAM Type": "DDR4/DDR5" if virtual_mem.total > 0 else "Unknown",
        "DRAM Speed": "Not available in Python",
        "Total DRAM Capacity": f"{virtual_mem.total / (1024**3):.2f} GB"
    }
    return mem_info

def get_nvme_storage_info():
    partitions = psutil.disk_partitions()
    storage_info = []
    for partition in partitions:
        if '/dev/nvme' in partition.device:  # Filter only /dev/nvme devices
            usage = psutil.disk_usage(partition.mountpoint)
            storage_info.append({
                "Device": partition.device,
                "Mountpoint": partition.mountpoint,
                "Filesystem": partition.fstype,
                "Total Capacity": f"{usage.total / (1024**3):.2f} GB",
                "Used Capacity": f"{usage.used / (1024**3):.2f} GB",
                "Free Capacity": f"{usage.free / (1024**3):.2f} GB"
            })
    return storage_info

def get_gpu_info():
    gpu_info = "No GPU detected or unable to access GPU information"
    try:
        gpu_output = subprocess.check_output("lspci | grep -i 'vga\\|3d\\|display'", shell=True, text=True)
        gpu_info = gpu_output.strip() if gpu_output else gpu_info
    except subprocess.CalledProcessError:
        pass
    return gpu_info

def get_os_info():
    os_info = {
        "OS Name": platform.system(),
        "OS Version": platform.version(),
        "Kernel Version": platform.release()
    }
    return os_info

def get_system_info():
    sys_info = {
        "CPU Info": get_cpu_info(),
        "Memory Info": get_memory_info(),
        "NVMe Storage Info": get_nvme_storage_info(),
        "GPU Info": get_gpu_info(),
        "OS Info": get_os_info()
    }
    return sys_info

def print_system_info():
    sys_info = get_system_info()
    for category, info in sys_info.items():
        print(f"{category}:")
        if isinstance(info, dict):
            for key, value in info.items():
                print(f"  {key}: {value}")
        elif isinstance(info, list):
            if not info:  # Check if list is empty
                print("  No NVMe storage devices found")
            for item in info:
                for key, value in item.items():
                    print(f"  {key}: {value}")
                print()
        else:
            print(f"  {info}")
        print()

print_system_info()
