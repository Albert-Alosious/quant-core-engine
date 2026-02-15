add_library(core_lib
  src/dummy_strategy.cpp
  src/event_bus.cpp
  src/event_loop_thread.cpp
)
target_include_directories(core_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
