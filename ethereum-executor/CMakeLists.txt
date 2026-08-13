cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(ethereum-executor
    EthereumExecutor.cpp
    EVMPrecompiles.cpp
    OpStackTransition.cpp
)
target_include_directories(ethereum-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/ethereum-executor>)
# bcosevm::opstack: the OP Stack lane calls bcos-evm's opTransition/runDeposit
# directly (single consensus implementation, see OpStackTransition.h).
# protocol-tars: reassembleWeb3RawTransaction (the signed envelope the L1 data
# fee is priced over). codec: the RLP primitives of the 0x7E deposit decoder.
target_link_libraries(ethereum-executor PUBLIC
    bcos-framework bcos-protocol bcosevm::opstack codec protocol-tars
    evmone::evmone evmone::precompiles intx::intx TBB::tbb)
set_target_properties(ethereum-executor PROPERTIES UNITY_BUILD OFF)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
