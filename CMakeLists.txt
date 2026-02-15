cmake_minimum_required(VERSION 3.20)
project(QuantCoreEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(core)

add_executable(quant_engine main.cpp)
target_link_libraries(quant_engine core_lib)