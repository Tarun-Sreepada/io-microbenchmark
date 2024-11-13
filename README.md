# IO Microbenchmark

This repository contains a simple IO microbenchmark tool to measure the performance of various IO operations.

## Building the Project

To build the project, follow these steps:

1. Create a build directory:
    ```sh
    mkdir build
    ```

2. Navigate to the build directory:
    ```sh
    cd build
    ```

3. Run CMake to configure the project:
    ```sh
    cmake ..
    ```

4. Build the project:
    ```sh
    make
    ```

## Running the Benchmark

To see the available options for running the benchmark, use the `--help` flag:
```sh
./io_benchmark --help
```

This will display the usage information and available options for the benchmark tool.

## Python Benchmark Script

The repository also contains a Python script that can be used to run the benchmark multiple times and generate a summary of the results. To use the script, follow these steps:

1. Make sure you have Python installed on your system.
2. Modify the `benchmark.py` script to specify the parameters you want to use for the benchmark.
3. Run the script as sudo to access /dev/{device}:
    ```sh
    sudo python benchmark.py
    ```