cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(opstack-executor
    OpBlockExecute.cpp
)
target_include_directories(opstack-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # Public headers use repository-root-qualified includes such as
    # <opstack-executor/...>, <ethereum-executor/...>, and <bcos-evm/...>.
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/opstack-executor>)
# `ledger` is explicit because OpBlockExecute.h exposes HashBuilder-backed templates and
# OpBlockExecute.cpp calls the trie builders directly; do not rely on bcos-evm-eth's PUBLIC
# closure. Web3TxEnvelope is header-only here, so no rlp-protocol link is required.
target_link_libraries(opstack-executor PUBLIC
    bcos-evm-opstack ethereum-executor bcos-framework bcos-protocol ledger
    evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # PUBLIC: OpstackExecutor.h / OpBlockExecute.h / Storage2State.h still instantiate
    # templates in consumer TUs (opstack-executor-tests). PRIVATE would hide
    # the -Wno-error flags from those instantiations.
    target_compile_options(opstack-executor PUBLIC
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
