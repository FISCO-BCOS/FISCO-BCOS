cmake_minimum_required(VERSION 3.25)

# OpstackExecutor is header-only (a template driven by the storage type), so the module is an
# INTERFACE library. It reuses the ethereum-executor adapter (StorageStateView, state-diff
# writeback, receipt conversion) and drives the OP transition pipeline from bcos-evm-opstack.
#
# DEPENDENCY: the `ethereum-executor` target (PR #5366). The add_subdirectory in the repo-root
# CMakeLists is guarded on this target existing so the tree stays buildable otherwise.

add_library(opstack-executor INTERFACE)
target_include_directories(opstack-executor INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(opstack-executor INTERFACE
    ethereum-executor      # adapter: StorageStateView, applyStateDiff, evmoneReceiptToBcos (PR #5366)
    bcos-evm-opstack       # opValidate / opTransition / runDeposit / finalizeOpBlock / OpFeeParams
    bcos-evm-eth
    bcos-framework)

if (TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()
