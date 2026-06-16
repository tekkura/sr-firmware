cmake_minimum_required(VERSION 3.13)

project(benchmark_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LATENCY_BENCHMARK "Enable firmware latency benchmark response size" OFF)

add_executable(benchmark main.cpp)

if(LATENCY_BENCHMARK)
    target_compile_definitions(benchmark PRIVATE LATENCY_BENCHMARK=1)
endif()

