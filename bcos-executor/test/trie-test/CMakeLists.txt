add_executable(trie-test main.cpp)
target_include_directories(trie-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../src/dag/)
# target_link_libraries(trie-test PUBLIC executor TBB::tbb)

