# Legacy code include path — provides access to legacy headers under legacy/
add_library(bcos-legacy INTERFACE)
target_include_directories(bcos-legacy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Legacy TiKV storage (moved out of bcos-storage, deprecated)
add_subdirectory(bcos-storage)

if(TESTS)
    enable_testing()
    add_subdirectory(bcos-ledger/test)
endif()
