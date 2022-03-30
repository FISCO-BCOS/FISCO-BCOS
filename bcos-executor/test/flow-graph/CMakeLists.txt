add_executable(flow-graph-test main.cpp)
target_include_directories(flow-graph-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../src/dag/)
target_link_libraries(flow-graph-test PUBLIC executor TBB::tbb)

