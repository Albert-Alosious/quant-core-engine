# -----------------------------------------------------------------------------
# Test targets — one executable per test suite for isolation.
# Each links against core_lib (or engine_lib) and GTest::gtest_main
# (provides int main() with GoogleTest runner so test files are pure tests).
# gtest_discover_tests() auto-registers individual TEST() cases with ctest.
# -----------------------------------------------------------------------------

include(GoogleTest)

# --- ThreadSafeQueue tests ---------------------------------------------------
add_executable(thread_safe_queue_test thread_safe_queue_test.cpp)
target_link_libraries(thread_safe_queue_test core_lib GTest::gtest_main)
gtest_discover_tests(thread_safe_queue_test)

# --- EventBus tests ----------------------------------------------------------
add_executable(event_bus_test event_bus_test.cpp)
target_link_libraries(event_bus_test core_lib GTest::gtest_main)
gtest_discover_tests(event_bus_test)

# --- Full Pipeline integration tests -----------------------------------------
add_executable(pipeline_integration_test pipeline_integration_test.cpp)
target_link_libraries(pipeline_integration_test core_lib GTest::gtest_main)
gtest_discover_tests(pipeline_integration_test)

# --- TradingEngine tests -----------------------------------------------------
add_executable(trading_engine_test trading_engine_test.cpp)
target_link_libraries(trading_engine_test engine_lib GTest::gtest_main)
gtest_discover_tests(trading_engine_test)
