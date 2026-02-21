add_library(core_lib
  src/statergy/dummy_strategy.cpp
  src/risk/risk_engine.cpp
  src/execution/execution_engine.cpp
  src/event/event_bus.cpp
  src/event/event_loop_thread.cpp
)
target_include_directories(core_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
