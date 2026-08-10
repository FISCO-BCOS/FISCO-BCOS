cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(ethereum-executor
    BCOS2Evmone.cpp
    EthereumExecutor.cpp
    StorageStateView.cpp
    EVMPrecompiles.cpp
)
target_include_directories(ethereum-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/ethereum-executor>)
target_link_libraries(ethereum-executor PUBLIC
    bcos-evm-eth bcos-framework bcos-protocol
    evmone::evmone evmone::precompiles intx::intx TBB::tbb)
set_target_properties(ethereum-executor PROPERTIES UNITY_BUILD OFF)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
