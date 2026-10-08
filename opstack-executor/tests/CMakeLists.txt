find_package(Boost REQUIRED unit_test_framework thread)
find_package(jsoncpp CONFIG REQUIRED)
find_package(evmone REQUIRED)
find_package(CURL REQUIRED)

# Lightweight suite: FakeTransaction / header-only surfaces.
# This binary links ledger via opstack-executor's PUBLIC closure (OpScheduler.h's cross-check
# and OpEthBlockSteps.h call the trie builders). The older "do not link ledger — wedprcrypto
# breaks libc++ typed catch binary-wide" instruction is false on this preset:
# BOOST_CHECK_THROW(..., OpConsensusError) is what CI runs.
#
# BOOST_TEST_MODULE lives in TestMain.cpp (the single main for this suite).
add_executable(opstack-executor-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    OpRecentBlockHashesTest.cpp
    OpDepositEnvelopeTest.cpp
    SmokeCompileTest.cpp
    OpSchedulerPolicyTest.cpp
    support/SeedPreStateTest.cpp
    OpSchedulerSeamSmokeTest.cpp
    OpRollupCostGoldenTest.cpp
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
# Locate the bcos-evm osaka fixture manifest (sha256 provenance check) at runtime.
target_compile_definitions(opstack-executor-tests PRIVATE
    OPSTACK_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
# The OP templates instantiate in these consumer TUs, so the GNU downgrades that the library
# marks PRIVATE (opstack-executor/CMakeLists.txt) must be re-added here explicitly — keeping
# them out of every other downstream target.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(opstack-executor-tests PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

# Post-cutover port-backlog suites that compile against CURRENT headers.
# The remaining suites (OpT8nReplayTest, OpNewPayloadRpcE2eTest, OpKarstActivationTest,
# OpL1BlockDepositTest, OpL1EdgeGateTest, OpBlockInjectorTest, OpDualPathEquivalenceTest,
# OpEngineBranchSmokeTest) reference pre-cutover headers (OpBlockExecute.h,
# OpDepositEncode.h, OpstackExecutor.h, OpForkFlagsCompat.h, OpSchedulerSeamTestHelpers.h)
# that must be ported first — tracked in the PR backlog.
add_executable(opstack-executor-block-tests
    TestMain.cpp
    OpDaMatrixSchemaTest.cpp
    OpGoldenCorpusProvenanceTest.cpp
    OpKarstReleaseGateTest.cpp
    OpMismatchedFieldTest.cpp
    support/GoldenSampleTest.cpp
    support/KarstNutHelpers.cpp
)
target_include_directories(opstack-executor-block-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/opstack-executor/tests
    ${CMAKE_SOURCE_DIR}/bcos-rpc
)
target_compile_definitions(opstack-executor-block-tests PRIVATE
    ${_t8n_defs}
    OP_DA_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json"
    OPSTACK_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(opstack-executor-block-tests PRIVATE
    opstack-executor
    jsoncpp_static
    Boost::unit_test_framework
)
# GoldenSampleTest reads the t8n corpus — same availability semantics as the
# scheduler-tests target (skip when absent, required in CI).
set_source_files_properties(TestMain.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

# Runtime suite (OpstackExecutorTest.cpp) stays with the web3-model decoupling follow-up.
# Scheduler execute/commit/call coverage is opstack-executor-scheduler-tests below.

# The old opstack-executor-receipt-tests target (OpReceiptEncodeTest / OpSealForkMatrixTest /
# OpCallWritebackTest over the legacy bcos-evm opstack layer) was retired in step 3.3 with the
# old layer's test surface; the receipts-root leaf encoding is covered by OpEthCommitmentsTest /
# OpEthForkMatrixTest in the scheduler binary below. The evmone test-utils / blst discovery it
# needed went with it.

# Scheduler suite: real executeBlock / commitBlock / call / callAtBlock against OpScheduler<MLS>.
# Isolated from the lightweight binary (ledger / protocol-tars / wedprcrypto typed-catch).
# Call fixtures use a FakeCallTx (no Web3Transaction.cpp — that RLP bind is #5496).
# ReorgUndo codec cases return with the tip-reorg follow-up.
add_executable(opstack-executor-scheduler-tests
    TestMain.cpp
    OpSchedulerTest.cpp
    OpBlockVerifierTest.cpp
    OpSepoliaReplayTest.cpp
    OpEthForkMatrixTest.cpp
    OpStorageErrorGuardTest.cpp
    OpEthEnvelopeCheckTest.cpp
    OpEthCommitmentsTest.cpp
    OpEthL1AttributesTest.cpp
    OpEthExecutorTest.cpp
    OpEthExecutorDualRunTest.cpp
    OpEthReceiptGoldenTest.cpp
    OpEthDepositGasTest.cpp
)
set_target_properties(opstack-executor-scheduler-tests PROPERTIES UNITY_BUILD OFF)
target_include_directories(opstack-executor-scheduler-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
)
# C1 golden harness (OpEthExecutorDualRunTest) reads the t8n corpus — same locations and override
# semantics as the engine parity suite (engine/test/CMakeLists.txt).
if(NOT DEFINED OP_T8N_VECTORS_DIR)
    set(OP_T8N_VECTORS_DIR "${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/vectors")
endif()
if(NOT DEFINED OP_T8N_GOLDEN_ENGINE_DIR)
    set(OP_T8N_GOLDEN_ENGINE_DIR
        "${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/golden/engine")
endif()
set(_t8n_defs
    OP_T8N_VECTORS_DIR="${OP_T8N_VECTORS_DIR}"
    OP_T8N_GOLDEN_ENGINE_DIR="${OP_T8N_GOLDEN_ENGINE_DIR}")
if(REQUIRE_T8N_CORPUS OR DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{CI})
    list(APPEND _t8n_defs FISCO_REQUIRE_T8N_CORPUS=1)
endif()
target_compile_definitions(opstack-executor-scheduler-tests PRIVATE ${_t8n_defs})
target_link_libraries(opstack-executor-scheduler-tests PRIVATE
    opstack-executor
    protocol-tars
    bcos-crypto
    codec
    ledger
    tool
    evmone::evmone
    Boost::unit_test_framework
    Boost::thread
    CURL::libcurl
    jsoncpp_static
)
add_test(NAME OpstackExecutorSchedulerTests COMMAND opstack-executor-scheduler-tests)

# OP templates instantiate in the consuming test TUs while opstack-executor marks its GNU
# downgrades PRIVATE — re-add the same flags here, identical to the sibling targets above.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(opstack-executor-scheduler-tests PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()

# OpEngineServiceParityTest lives in engine/test (S5); the t8n corpus vectors/goldens are
# tracked under opstack-executor/tests/t8n/ and are exercised by the engine exec parity suite.
