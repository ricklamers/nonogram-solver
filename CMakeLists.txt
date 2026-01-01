cmake_minimum_required(VERSION 3.14)
project(nonogram_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find pthreads
find_package(Threads REQUIRED)

# Core library (shared between executables)
add_library(nonogram_core STATIC
    src/nonogram.cpp
)
target_include_directories(nonogram_core PUBLIC src)

# Fast solver (single puzzle, hybrid propagation + SAT)
add_executable(nonogram_solver
    src/fast_solver.cpp
)
target_link_libraries(nonogram_solver PRIVATE nonogram_core Threads::Threads)

# Fast benchmark (batch processing with JSON output)
add_executable(nonogram_bench
    src/fast_benchmark.cpp
)
target_link_libraries(nonogram_bench PRIVATE nonogram_core Threads::Threads)

# Verification test suite
add_executable(nonogram_verify
    src/verify.cpp
)
target_link_libraries(nonogram_verify PRIVATE nonogram_core)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nonogram_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_solver PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_bench PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_verify PRIVATE -Wall -Wextra -Wpedantic)
endif()
