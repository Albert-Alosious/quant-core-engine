add_library(engine_lib
  src/trading_engine.cpp
)
target_include_directories(engine_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine_lib PUBLIC core_lib)
