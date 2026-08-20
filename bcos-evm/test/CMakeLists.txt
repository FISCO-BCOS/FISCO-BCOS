find_package(Boost REQUIRED unit_test_framework)

add_executable(bcos-evm-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-eth-tests PRIVATE
    bcosevm::eth
    Boost::unit_test_framework
)
add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)

add_executable(bcos-evm-opstack-tests
    opstack/TestMain.cpp
    opstack/OpForkScheduleTest.cpp
    opstack/OpHostTest.cpp
    opstack/OpPrecompilesTest.cpp
    opstack/OpFeeParamsTest.cpp
    opstack/OpDepositTxTest.cpp
    opstack/OpPredeploysTest.cpp
    opstack/RollupCostTest.cpp
    opstack/OpValidateTest.cpp
    opstack/OpTransitionTest.cpp
    opstack/OpDepositTest.cpp
    opstack/OpZeroDiffTest.cpp
    opstack/OpReceiptMetaTest.cpp
    opstack/Op7702Test.cpp
    opstack/OpFloorGasTest.cpp
    opstack/OpZeroFeeSpikeTest.cpp
    opstack/OpL1BlockRealBytecodeTest.cpp
)
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    Boost::unit_test_framework
)
# evmone_precompiles' bare -lblst needs the vcpkg lib dir on the search path (shared-vcpkg
# worktree layout; same fix as opstack-executor/tests/CMakeLists.txt).
find_library(BLST_EVM_LIBRARY NAMES blst PATHS
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib"
    NO_DEFAULT_PATH REQUIRED)
get_filename_component(BLST_EVM_LIB_DIR ${BLST_EVM_LIBRARY} DIRECTORY)
target_link_directories(bcos-evm-opstack-tests PRIVATE ${BLST_EVM_LIB_DIR})

# OpEnvelopeToTarsTest is the sole guard-block test kept in bcos-evm: it verifies
# opEnvelopeToTars' sender derivation against known-signature envelopes. It is in-tree-only —
# it needs bcos-framework targets (codec / protocol-tars / ledger / engine / rpc) that exist only
# when the top-level CMakeLists.txt add_subdirectory(bcos-framework) runs first. It includes
# <engine/bcos-engine/EngineServiceImpl.h> (needs the repo root on the include path) and needs
# `engine` + `rpc` for the non-template helpers / EngineHelper symbols; the jsoncpp link covers
# EngineServiceImpl.h's include chain (bcos-rpc/jsonrpc/Common.h). `ledger`/`engine`/`rpc` are
# added by the root CMakeLists.txt AFTER bcos-evm, so the target references resolve at generate
# time (same pattern as the source branch's test CMake).
if(TARGET bcos-framework)
    find_package(jsoncpp CONFIG REQUIRED)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/OpEnvelopeToTarsTest.cpp
    )
    target_link_libraries(bcos-evm-opstack-tests PRIVATE
        codec protocol-tars ledger engine bcos-utilities jsoncpp_static rpc)
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-ledger
        ${CMAKE_SOURCE_DIR}/bcos-rpc)
endif()
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
