cmake_minimum_required(VERSION 3.25)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(opstack-executor
    OpFeeParams.cpp
    OpRollupCost.cpp
    OpEnvelopeCheck.cpp
    OpEthCommitments.cpp
    OpEthL1Attributes.cpp
)
target_include_directories(opstack-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # Public headers use repository-root-qualified includes such as
    # <opstack-executor/...> and <ethereum-executor/...>.
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/opstack-executor>)
# `ledger` is explicit because OpScheduler.h / OpEthBlockSteps.h call the trie builders and
# StateRoots helpers directly; do not rely on a transitive PUBLIC closure. `rlp-protocol` is
# explicit because web3ChainIdFromEnvelope lives in that library TU (typed catch was measured
# to bind even with bcos-crypto on the link line).
target_link_libraries(opstack-executor PUBLIC
    ethereum-executor bcos-framework bcos-protocol ledger rlp-protocol
    transaction-scheduler
    # bcos-devp2p: OpBlockVerifier.h's public surface takes devp2p::sync::Block (header-only
    # sync/Block.h; the link carries its include dir to consumers).
    bcos-devp2p
    # protocol-tars: OpBlockVerifier.h wraps p2p envelopes into bcostars::protocol::
    # TransactionImpl and calls Web3Transaction::takeToTarsTransaction (defined in
    # bcos-tars-protocol's Web3TarsBridge.cpp).
    protocol-tars
    evmone::evmone intx::intx TBB::tbb)
set_target_properties(opstack-executor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # PRIVATE: the OP templates (OpEthExecutor.h / OpEthBlockSteps.h / OpScheduler.h)
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
