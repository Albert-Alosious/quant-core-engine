add_library(core_lib
  src/statergy/dummy_strategy.cpp
  src/risk/risk_engine.cpp
  src/risk/position_engine.cpp
  src/risk/order_tracker.cpp
  src/execution/execution_engine.cpp
  src/execution/mock_execution_engine.cpp
  src/event/event_bus.cpp
  src/event/event_loop_thread.cpp
  src/time/live_time_provider.cpp
  src/time/simulation_time_provider.cpp
  src/gateway/market_data_gateway.cpp
)
target_include_directories(core_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(core_lib PUBLIC
  cppzmq
  nlohmann_json::nlohmann_json
  Threads::Threads
)
