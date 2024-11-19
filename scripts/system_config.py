import platform
import psutil
import subprocess
import json

def get_cpu_info():
    cpu_info = {
        "CPU Sockets": 1,
        "CPU Cores": psutil.cpu_count(logical=False),
        "CPU Threads": psutil.cpu_count(logical=True),
        "CPU Names": []
    }
    
    # Default CPU name using platform info
    cpu_name = platform.processor()
    
    if platform.system() == "Linux":
        try:
            socket_count = 0
            cpu_names = set()
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if "physical id" in line:
                        socket_id = int(line.strip().split(":")[1].strip())
                        socket_count = max(socket_count, socket_id + 1)
                    if "model name" in line:
                        cpu_name = line.strip().split(":")[1].strip()
                        cpu_names.add(cpu_name)
            cpu_info["CPU Sockets"] = socket_count
            cpu_info["CPU Names"] = list(cpu_names)
        except FileNotFoundError:
            cpu_info["CPU Names"] = [cpu_name]  # Fallback for non-Linux systems

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
    # Run lsblk to get information on all block devices in JSON format, including the MODEL attribute
    result = subprocess.run(['lsblk', '-J', '-o', 'NAME,SIZE,MOUNTPOINT,FSTYPE,MODEL'], capture_output=True, text=True)
    devices = json.loads(result.stdout)["blockdevices"]

    storage_info = []
    for device in devices:
        if "nvme" in device["name"]:  # Check for NVMe devices
            device_info = {
                "Device": f"/dev/{device['name']}",
                "Model": device["model"] if "model" in device else "Unknown",
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
                        "Model": device_info["Model"],  # Partitions have the same model as the parent device
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

def print_system_info_summary():
    sys_info = get_system_info()
    
    # Extract CPU information
    cpu_name = sys_info["CPU Info"]["CPU Names"][0] if sys_info["CPU Info"]["CPU Names"] else "Unknown CPU"
    cpu_cores = sys_info["CPU Info"]["CPU Cores"]
    print(f"CPU: {cpu_name}, Cores: {cpu_cores}")
    
    # Extract DRAM information
    dram_type = sys_info["Memory Info"]["DRAM Type"]
    dram_capacity = sys_info["Memory Info"]["Total DRAM Capacity"]
    print(f"DRAM: {dram_type}, Capacity: {dram_capacity}")
    
    # Extract the first NVMe storage device information, if available
    nvme_devices = sys_info["NVMe Storage Info"]
    if nvme_devices:
        first_nvme = nvme_devices[0]
        nvme_model = first_nvme["Model"]
        nvme_size = first_nvme["Size"]
        nvme_mountpoint = first_nvme["Mountpoint"]
        print(f"NVMe: Model: {nvme_model}, Size: {nvme_size}, Mountpoint: {nvme_mountpoint}")
    else:
        print("NVMe: No NVMe storage devices found")

print_system_info_summary()

# print_system_info()
