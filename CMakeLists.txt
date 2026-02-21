cmake_minimum_required(VERSION 3.20)
project(QuantCoreEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(core)

add_executable(quant_engine main.cpp)
target_link_libraries(quant_engine engine_lib)

# -----------------------------------------------------------------------------
# Google Test (fetched at configure time — no system install needed)
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
