# Legacy code include path — provides access to legacy headers under legacy/
add_library(bcos-legacy INTERFACE)
target_include_directories(bcos-legacy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(TESTS)
    enable_testing()
    add_subdirectory(bcos-ledger/test)
endif()
