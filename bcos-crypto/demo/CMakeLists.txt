find_package(ZLIB REQUIRED)
find_package(ethash QUIET)

if(ethash_FOUND)
    add_executable(perf_demo perf_demo.cpp)
    target_link_libraries(perf_demo PRIVATE bcos-crypto ZLIB::ZLIB ethash::keccak)
endif()
