cmake_minimum_required(VERSION 3.30)
project(scale)

set(CMAKE_CXX_STANDARD 20)

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
endif()


Include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(Catch2)

include(FetchContent)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.7.0
)
FetchContent_MakeAvailable(yaml-cpp)


add_library(scale_core
        src/benchmark_types.cpp
        src/curl.cpp
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
target_link_libraries(scale_core PUBLIC yaml-cpp::yaml-cpp)


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