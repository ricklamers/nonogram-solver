cmake_minimum_required(VERSION 3.14)
project(nonogram_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find dependencies
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# ==============================================================================
# Core library (shared between executables)
# ==============================================================================
add_library(nonogram_core STATIC
    src/nonogram.cpp
)
target_include_directories(nonogram_core PUBLIC src)

# ==============================================================================
# Glucose SAT Solver (bundled)
# ==============================================================================
set(GLUCOSE_DIR ${CMAKE_SOURCE_DIR}/glucose-sat)

# Glucose Parallel (Syrup) library - includes both sequential and parallel
add_library(glucose STATIC
    ${GLUCOSE_DIR}/core/Solver.cc
    ${GLUCOSE_DIR}/core/lcm.cc
    ${GLUCOSE_DIR}/simp/SimpSolver.cc
    ${GLUCOSE_DIR}/utils/Options.cc
    ${GLUCOSE_DIR}/utils/System.cc
    ${GLUCOSE_DIR}/parallel/ParallelSolver.cc
    ${GLUCOSE_DIR}/parallel/MultiSolvers.cc
    ${GLUCOSE_DIR}/parallel/SharedCompanion.cc
    ${GLUCOSE_DIR}/parallel/SolverCompanion.cc
    ${GLUCOSE_DIR}/parallel/SolverConfiguration.cc
    ${GLUCOSE_DIR}/parallel/ClausesBuffer.cc
)
target_include_directories(glucose PUBLIC ${GLUCOSE_DIR})
target_compile_options(glucose PRIVATE -std=c++11 -w)  # Suppress warnings from glucose
target_link_libraries(glucose PUBLIC ZLIB::ZLIB Threads::Threads)

# ==============================================================================
# Main Executables
# ==============================================================================

# RECOMMENDED: Hybrid solver (propagation + Glucose-Syrup parallel SAT)
# Self-contained, no external dependencies, best performance
add_executable(nonogram_hybrid
    src/hybrid_solver.cpp
)
target_include_directories(nonogram_hybrid PRIVATE ${GLUCOSE_DIR})
target_link_libraries(nonogram_hybrid PRIVATE nonogram_core glucose)

# Legacy: External SAT solver version (requires kissat/cryptominisat installed)
add_executable(nonogram_solver
    src/fast_solver.cpp
)
target_link_libraries(nonogram_solver PRIVATE nonogram_core Threads::Threads)

# Benchmark runner (batch processing with JSON output)
add_executable(nonogram_bench
    src/fast_benchmark.cpp
)
target_link_libraries(nonogram_bench PRIVATE nonogram_core Threads::Threads)

# Verification test suite
add_executable(nonogram_verify
    src/verify.cpp
)
target_link_libraries(nonogram_verify PRIVATE nonogram_core)

# ==============================================================================
# Enable warnings for our code (not for glucose)
# ==============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nonogram_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_hybrid PRIVATE -Wall -Wextra)
    target_compile_options(nonogram_solver PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_bench PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nonogram_verify PRIVATE -Wall -Wextra -Wpedantic)
endif()
