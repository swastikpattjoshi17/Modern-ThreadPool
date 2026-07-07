cmake_minimum_required(VERSION 3.14)
project(ModernThreadPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Header-only library target
# ---------------------------------------------------------------------------
add_library(mtp INTERFACE)
target_include_directories(mtp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(mtp INTERFACE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(mtp INTERFACE Threads::Threads)

if(MSVC)
    target_compile_options(mtp INTERFACE /W4)
else()
    target_compile_options(mtp INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MTP_BUILD_TESTS "Build the test suite" ON)
option(MTP_BUILD_EXAMPLES "Build example programs" ON)
option(MTP_BUILD_BENCHMARKS "Build benchmark programs" ON)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MTP_BUILD_TESTS)
    enable_testing()

    add_executable(test_thread_pool tests/test_thread_pool.cpp)
    target_link_libraries(test_thread_pool PRIVATE mtp)
    target_include_directories(test_thread_pool PRIVATE tests)
    add_test(NAME test_thread_pool COMMAND test_thread_pool)

    add_executable(test_parallel_algorithms tests/test_parallel_algorithms.cpp)
    target_link_libraries(test_parallel_algorithms PRIVATE mtp)
    target_include_directories(test_parallel_algorithms PRIVATE tests)
    add_test(NAME test_parallel_algorithms COMMAND test_parallel_algorithms)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(MTP_BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE mtp)

    add_executable(parallel_algorithms_demo examples/parallel_algorithms_demo.cpp)
    target_link_libraries(parallel_algorithms_demo PRIVATE mtp)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(MTP_BUILD_BENCHMARKS)
    add_executable(bench_thread_pool benchmarks/bench_thread_pool.cpp)
    target_link_libraries(bench_thread_pool PRIVATE mtp)
    if(NOT MSVC)
        target_compile_options(bench_thread_pool PRIVATE -O3)
    endif()
endif()
