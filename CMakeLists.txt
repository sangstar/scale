cmake_minimum_required(VERSION 3.30)
project(scale)

set(CMAKE_CXX_STANDARD 20)


Include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(Catch2)

add_library(scale_core
        src/benchmark_types.cpp
        src/curl.cpp
        src/dataset.cpp
        src/logger.cpp
        src/result_types.cpp
        src/utils.cpp
        src/streaming_response.cpp
)


# Add include paths
target_include_directories(scale_core PUBLIC "${PROJECT_SOURCE_DIR}/include")

# Link against CURL
find_package(CURL REQUIRED)
target_link_libraries(scale_core PRIVATE CURL::libcurl)

add_executable(scale src/main.cpp)
target_link_libraries(scale PRIVATE scale_core)

add_executable(tests tests/test.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain scale_core)
target_link_libraries(tests PRIVATE CURL::libcurl)


target_include_directories(scale PUBLIC "${PROJECT_SOURCE_DIR}/include")

find_package(CURL REQUIRED)
target_link_libraries(scale PRIVATE CURL::libcurl)

include(CTest)
include(Catch)
catch_discover_tests(tests)