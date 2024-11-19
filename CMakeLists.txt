cmake_minimum_required(VERSION 3.10)

# Set the project name
project(NVMeBenchmark)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the optimization level
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3")

# Find required packages
find_package(PkgConfig REQUIRED)

# Check for liburing
pkg_check_modules(LIBURING REQUIRED liburing)

# Include directories
include_directories(include ${LIBURING_INCLUDE_DIRS})

# Add the main executable
add_executable(io_benchmark
    src/main.cpp
    src/sync.cpp
    src/async.cpp
    src/config.cpp
)

# Link libraries to the main executable
target_link_libraries(io_benchmark PRIVATE ${LIBURING_LIBRARIES} pthread)

# Display configuration summary
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Optimization Level: ${CMAKE_CXX_FLAGS}")
message(STATUS "liburing Include Dirs: ${LIBURING_INCLUDE_DIRS}")
message(STATUS "liburing Libraries: ${LIBURING_LIBRARIES}")
