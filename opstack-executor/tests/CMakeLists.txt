find_package(GTest CONFIG REQUIRED)
find_package(evmone REQUIRED)
# rlp_encode.cpp was removed from the production bcos-evm-eth lib (zero production callers); the
# only consumer is OpReceiptEncodeTest's ADL call into evmone::state::rlp_encode. EVMONE_TEST_UTILS_DIR
# is defined in bcos-evm/CMakeLists.txt (sibling scope, not visible here), so re-derive it.
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
# rlp_encode.cpp: vendored evmone test util, compiled here (not in production lib). Same
# -Wno-missing-field-initializers as the bcos-evm production build applied to upstream code.
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
