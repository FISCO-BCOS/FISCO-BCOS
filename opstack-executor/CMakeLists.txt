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
# closure. `rlp-protocol` is explicit because web3ChainIdFromEnvelope lives in that library
# TU (typed catch was measured to bind even with bcos-crypto on the link line).
target_link_libraries(opstack-executor PUBLIC
    bcos-evm-opstack ethereum-executor bcos-framework bcos-protocol ledger rlp-protocol
    transaction-scheduler
    evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # PRIVATE: the OP templates (OpstackExecutor.h / OpBlockExecute.h / Storage2State.h)
    # instantiate in the test TUs, so those binaries add the flags explicitly (tests/
    # CMakeLists.txt). PUBLIC would propagate the downgrades to every future consumer —
    # including the node binary once part-5 links the library — which
    # cmake/CompilerSettings.cmake:118-129 forbids ("would silence a real defect detector
    # (-Wmissing-field-initializers) for all 25+ modules on behalf of one feature"; the
    # per-target downgrade is the documented mechanism, cf. bcos-evm/CMakeLists.txt).
    target_compile_options(opstack-executor PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
