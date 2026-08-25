find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)
find_package(evmone REQUIRED)

# Lightweight suite: FakeTransaction / header-only surfaces. Do not link ledger / rpc /
# protocol-tars — that chain pulls wedprcrypto and breaks libc++ typed catch binary-wide
# (bcos-rpc/test/CMakeLists.txt:29-34).
#
# BOOST_TEST_MODULE lives in TestMain.cpp (the single main for this suite).
add_executable(opstack-executor-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    OpDepositEnvelopeTest.cpp
    OpTxConvertTest.cpp
    SmokeCompileTest.cpp
    OpCommonTest.cpp
    OpCommitmentsTest.cpp
    RecentBlockHashesTest.cpp
    PreBlockOpStepsTest.cpp
    Storage2StateHelpersTest.cpp
    Storage2StatePoisonTest.cpp
    OpEnvelopeMirrorTest.cpp
    support/SeedPreStateTest.cpp
    OpJovianShapeTest.cpp
    OpSchedulerSeamSmokeTest.cpp
)
# Unity build: header-heavy Boost.Test TUs share a large common preamble. TestMain.cpp
# defines BOOST_TEST_MODULE. SeamSmoke has an anonymous-namespace CheckpointStorage stub
# that would collide with SeedPreStateTest.cpp under unity merge.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD "ON")
set_source_files_properties(TestMain.cpp OpSchedulerSeamSmokeTest.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor
    jsoncpp_static
    Boost::unit_test_framework
)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

# Runtime suite: real tars TransactionImpl / Web3Transaction / receipt factory.
# SPLIT OUT (tracks in the next PR): it compiles base bcos-rpc model TUs
# (Web3Transaction.cpp / Web3TxHandler.cpp) whose EIP-7702 auth-list RLP support lands
# in the RPC split (#5496) — building them here breaks the gcc build. The end-to-end
# executor tests (OpstackExecutorTest.cpp, incl. the envelope-vs-mirror chainId cases)
# return with the web3-model decoupling in the follow-up PR.


# Receipts-root leaf encoding (encodeReceiptForRoot / sealOpBlock). Needs evmone's packaged RLP
# helpers and ledger's computeTrieRootVarKey; kept off the lightweight binary for the same
# wedprcrypto reason. These dependencies are already mandatory for every FULLNODE build:
# bcos-evm/CMakeLists.txt requires rlp_encode.cpp and evmone's package creates the blst target.
# Build the consensus golden-vector suite unconditionally so CI cannot silently lose it.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")
if(NOT EXISTS "${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp")
    message(FATAL_ERROR "opstack-executor receipt tests require evmone test-utils "
        "(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp)")
endif()
if(NOT TARGET blst)
    message(FATAL_ERROR "opstack-executor receipt tests require the blst CMake target")
endif()
add_executable(opstack-executor-receipt-tests
    TestMain.cpp
    OpReceiptEncodeTest.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
)
set_target_properties(opstack-executor-receipt-tests PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_link_libraries(opstack-executor-receipt-tests PRIVATE
    opstack-executor
    protocol-tars
    bcos-crypto
    codec
    ledger
    evmone::evmone
    Boost::unit_test_framework
    blst
)
target_include_directories(opstack-executor-receipt-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
)
add_test(NAME OpstackExecutorReceiptTests COMMAND opstack-executor-receipt-tests)
