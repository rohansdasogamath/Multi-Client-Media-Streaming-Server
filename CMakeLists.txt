cmake_minimum_required(VERSION 3.16)
project(MultiClientMediaStreamingServer
    VERSION 1.0.0
    DESCRIPTION "High-performance multi-client TCP media streaming server in modern C++17"
    LANGUAGES CXX
)

# Enforce C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual)
endif()

# Find POSIX Threads
find_package(Threads REQUIRED)

# Common include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Core library
add_library(streamer_core STATIC
    src/Logger.cpp
    src/Socket.cpp
    src/FileManager.cpp
    src/Protocol.cpp
    src/ClientSession.cpp
    src/Server.cpp
)

target_include_directories(streamer_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(streamer_core PUBLIC
    Threads::Threads
)

# Server executable
add_executable(media_server src/server_main.cpp)
target_link_libraries(media_server PRIVATE streamer_core)

# Client executable
add_executable(media_client src/client_main.cpp)
target_link_libraries(media_client PRIVATE streamer_core)

# Automated testing
enable_testing()
add_executable(streamer_tests tests/test_streaming.cpp)
target_link_libraries(streamer_tests PRIVATE streamer_core)

add_test(NAME streamer_tests COMMAND streamer_tests)

# Concurrency & multi-client stress testing
add_executable(test_concurrency tests/test_concurrency.cpp)
target_link_libraries(test_concurrency PRIVATE streamer_core)
add_test(NAME test_concurrency COMMAND test_concurrency)
