cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(opstack-executor
    OpBlockExecute.cpp
)
target_include_directories(opstack-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/opstack-executor>)
target_link_libraries(opstack-executor PUBLIC
    bcos-evm-opstack ethereum-executor bcos-framework bcos-protocol
    ledger evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
