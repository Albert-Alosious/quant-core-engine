cmake_minimum_required(VERSION 3.20)
project(QuantCoreEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Threading support (required on Linux; harmless on macOS)
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# ZeroMQ — system-installed C library (brew install zeromq)
# find_path/find_library used because not all systems ship a CMake config.
# -----------------------------------------------------------------------------
find_path(ZeroMQ_INCLUDE_DIR zmq.h
  HINTS /opt/homebrew/include /usr/local/include /usr/include)
find_library(ZeroMQ_LIBRARY zmq
  HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)

if(NOT ZeroMQ_INCLUDE_DIR OR NOT ZeroMQ_LIBRARY)
  message(FATAL_ERROR
    "Could not find ZeroMQ (libzmq). Install it: brew install zeromq")
endif()

add_library(libzmq SHARED IMPORTED)
set_target_properties(libzmq PROPERTIES
  IMPORTED_LOCATION "${ZeroMQ_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${ZeroMQ_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# FetchContent dependencies (downloaded at configure time)
# -----------------------------------------------------------------------------
include(FetchContent)

# cppzmq — header-only C++ binding for ZeroMQ.
# cppzmq's CMakeLists calls find_package(ZeroMQ), which fails without a
# config file. We skip that by setting the variables it expects and fetching
# with CONFIGURE_COMMAND disabled via the OVERRIDE_FIND_PACKAGE approach:
# we simply add the header directory ourselves after downloading.
FetchContent_Declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG        v4.10.0
)
# Prevent cppzmq from running its own CMakeLists (it calls find_package
# internally). We fetch the source and create a header-only interface target.
FetchContent_GetProperties(cppzmq)
if(NOT cppzmq_POPULATED)
  FetchContent_Populate(cppzmq)
endif()
add_library(cppzmq INTERFACE)
target_include_directories(cppzmq INTERFACE ${cppzmq_SOURCE_DIR})
target_link_libraries(cppzmq INTERFACE libzmq)

# nlohmann/json — header-only JSON library for parsing ZMQ messages
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# Google Test
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

# -----------------------------------------------------------------------------
# Core libraries
# -----------------------------------------------------------------------------
add_subdirectory(core)

# -----------------------------------------------------------------------------
# Main executable
# -----------------------------------------------------------------------------
add_executable(quant_engine main.cpp)
target_link_libraries(quant_engine engine_lib Threads::Threads)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
