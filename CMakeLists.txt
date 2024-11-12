cmake_minimum_required(VERSION 3.10)

# Set the project name
project(NVMeBenchmark)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the optimization level
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

# Add the io_benchmark executable
add_executable(io_benchmark io_benchmark.cpp)

# Include directories for io_benchmark
target_include_directories(io_benchmark PRIVATE ${LIBURING_INCLUDE_DIRS})

# Link libraries to io_benchmark
target_link_libraries(io_benchmark PRIVATE ${LIBURING_LIBRARIES} pthread)

# Add the io_benchmark_sync executable
add_executable(io_benchmark_sync io_benchmark_sync.cpp)

# Include directories for io_benchmark_sync
target_include_directories(io_benchmark_sync PRIVATE ${LIBURING_INCLUDE_DIRS})

# Link libraries to io_benchmark_sync
# Only link pthread, as io_uring is not used in the synchronous version
target_link_libraries(io_benchmark_sync PRIVATE pthread)
