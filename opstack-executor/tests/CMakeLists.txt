find_package(GTest CONFIG REQUIRED)
find_package(evmone REQUIRED)
# rlp_encode.cpp left the production lib (zero callers); its only consumer, OpReceiptEncodeTest,
# ADL-calls evmone::state::rlp_encode. Re-derive EVMONE_TEST_UTILS_DIR here — set only in sibling bcos-evm scope.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")

add_executable(opstack-executor-tests OpstackExecutorTest.cpp)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor           # INTERFACE: pulls ethereum-executor + bcos-evm-opstack + bcos-evm-eth + bcos-framework
    protocol-tars              # TransactionImpl / BlockHeaderImpl / TransactionReceiptFactoryImpl
    bcos-crypto                # CryptoSuite / Keccak256
    codec                      # rlp::decode for the web3 tx fixture
    rpc                        # bcos::rpc::Web3Transaction (model .cpp)
    evmone::evmone
    GTest::gtest_main)
# Web3Transaction + tars aggregate init are not unity-build friendly here.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD OFF)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

add_executable(opstack-executor-block-tests
    TestMain.cpp
    OpT8nReplayTest.cpp
    OpReceiptEncodeTest.cpp
    OpL1EdgeGateTest.cpp
    OpNewPayloadRpcE2eTest.cpp
    OpSchedulerImplSmokeTest.cpp
    OpEngineBranchSmokeTest.cpp
    support/SeedPreStateTest.cpp
    support/GoldenSampleTest.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
)
# 与既有 GTest target 同因：Web3Transaction + tars aggregate init 不宜 unity-build
set_target_properties(opstack-executor-block-tests PROPERTIES UNITY_BUILD OFF)
# Vendored evmone test util — same -Wno-missing-field-initializers as the upstream build.
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_link_libraries(opstack-executor-block-tests PRIVATE
    opstack-executor bcosevm::opstack Boost::unit_test_framework
    codec protocol-tars ledger engine bcos-utilities jsoncpp_static rpc)
target_include_directories(opstack-executor-block-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
    ${CMAKE_SOURCE_DIR}/bcos-rpc)
target_compile_definitions(opstack-executor-block-tests PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/golden/engine")
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

# Extracted pure-function layer (OpRlpDecode.h / Storage2StateHelpers.h): direct unit tests
# over the decode primitives and state-bridge helpers, no class-template instantiation.
add_executable(opstack-executor-detail-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    Storage2StateHelpersTest.cpp
)
set_target_properties(opstack-executor-detail-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-executor-detail-tests PRIVATE
    opstack-executor codec evmone::evmone intx::intx Boost::unit_test_framework bcos-utilities)
add_test(NAME OpstackExecutorDetailTests COMMAND opstack-executor-detail-tests)
