cmake_minimum_required(VERSION 3.30)
project(scale)

set(CMAKE_CXX_STANDARD 20)

add_executable(scale src/main.cpp
        src/benchmarks/benchmark.hpp
        src/benchmarks/cola.hpp
        src/benchmarks/benchmark.cpp
        src/curl.cpp
        src/curl.hpp
        src/benchmarks/cola.cpp
        src/ring_buffers.cpp
        src/ring_buffers.hpp
        src/completion_types.cpp
        src/completion_types.hpp
        src/streaming_response.cpp
        src/streaming_response.hpp
        src/request_parameters.cpp
        src/request_parameters.hpp
        src/results.cpp
        src/results.hpp
        src/latency_metrics.cpp
        src/latency_metrics.hpp)

find_package(CURL REQUIRED)
target_link_libraries(scale PRIVATE CURL::libcurl)