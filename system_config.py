import platform
import psutil
import subprocess
import json


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
    # Run lsblk to get information on all block devices in JSON format
    result = subprocess.run(['lsblk', '-J', '-o', 'NAME,SIZE,MOUNTPOINT,FSTYPE'], capture_output=True, text=True)
    devices = json.loads(result.stdout)["blockdevices"]

    storage_info = []
    for device in devices:
        if "nvme" in device["name"]:  # Check for NVMe devices
            device_info = {
                "Device": f"/dev/{device['name']}",
                "Size": device["size"],
                "Mountpoint": device["mountpoint"] if device["mountpoint"] else "Not mounted",
                "Filesystem": device["fstype"] if device["fstype"] else "N/A",
            }
            storage_info.append(device_info)
            
            # If there are partitions under the NVMe device, add them as well
            if "children" in device:
                for partition in device["children"]:
                    partition_info = {
                        "Device": f"/dev/{partition['name']}",
                        "Size": partition["size"],
                        "Mountpoint": partition["mountpoint"] if partition["mountpoint"] else "Not mounted",
                        "Filesystem": partition["fstype"] if partition["fstype"] else "N/A",
                    }
                    storage_info.append(partition_info)
                    
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
