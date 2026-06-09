cmake_minimum_required(VERSION 3.20)
project(cleainput VERSION 0.1.0 LANGUAGES CXX)

# C++23 — Durov style, latest standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Maximum optimization flags
set(CMAKE_CXX_FLAGS_RELEASE
    "-O3 -march=native -mavx2 -mfma \
     -funroll-loops \
     -fno-exceptions \
     -DNDEBUG")

# All source files
file(GLOB_RECURSE SOURCES
    "src/core/*.cpp"
    "src/ai/*.cpp"
    "src/audio/*.cpp"
    "src/net/*.cpp"
    "src/crawler/*.cpp"
    "main.cpp"
)

# Main executable
add_executable(cleainput ${SOURCES})

# Include directories
target_include_directories(cleainput PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/ai
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/crawler
)

# Link threads
find_package(Threads REQUIRED)
target_link_libraries(cleainput PRIVATE
    Threads::Threads
)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Building: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
