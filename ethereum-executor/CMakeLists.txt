cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

# The ported evmone precompile dispatch (EVMPrecompiles.h/.cpp) is a standalone
# library: it is the shared canonical EVM-precompile implementation used by both
# this executor and transaction-executor (via EvmPrecompiledAdapter.h). Only its
# own directory is exported (consumers include <EVMPrecompiles.h>) — exporting
# the repository root would put every top-level name into the header search
# space of transaction-executor and everything downstream.
add_library(evm-precompiles STATIC
    EVMPrecompiles.cpp
)
target_include_directories(evm-precompiles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(evm-precompiles PUBLIC
    evmone::evmone evmone::precompiles intx::intx)
set_target_properties(evm-precompiles PROPERTIES UNITY_BUILD OFF)

add_library(ethereum-executor
    EthereumExecutor.cpp
)
target_include_directories(ethereum-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/ethereum-executor>)
target_link_libraries(ethereum-executor PUBLIC
    evm-precompiles bcos-framework bcos-protocol
    evmone::evmone evmone::precompiles intx::intx TBB::tbb)
set_target_properties(ethereum-executor PROPERTIES UNITY_BUILD OFF)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmark)
endif()
