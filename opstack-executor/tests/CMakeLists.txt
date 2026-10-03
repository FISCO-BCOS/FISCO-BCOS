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
# The OP templates instantiate in these consumer TUs, so the GNU downgrades that the library
# marks PRIVATE (opstack-executor/CMakeLists.txt) must be re-added here explicitly — keeping
# them out of every other downstream target.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(opstack-executor-tests PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

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

# Block-level t8n replay + engine RPC e2e (full corpus external; see tools/.ci/provision_t8n_corpus.sh).
add_executable(opstack-executor-block-tests
    TestMain.cpp
    OpT8nReplayTest.cpp
    OpNewPayloadRpcE2eTest.cpp
    OpL1EdgeGateTest.cpp
    OpL1BlockDepositTest.cpp
    OpBlockInjectorTest.cpp
    OpEngineBranchSmokeTest.cpp
    OpDualPathEquivalenceTest.cpp
    OpGoldenCorpusProvenanceTest.cpp
    OpDaMatrixSchemaTest.cpp
    OpKarstActivationTest.cpp
    OpKarstReleaseGateTest.cpp
    support/GoldenSampleTest.cpp
    support/KarstNutHelpers.cpp
)
set_target_properties(opstack-executor-block-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-executor-block-tests PRIVATE
    opstack-executor
    bcosevm::opstack
    codec
    protocol-tars
    ledger
    engine
    bcos-utilities
    jsoncpp_static
    rpc
    transaction-scheduler
    evmone::evmone
    Boost::unit_test_framework
    blst
)
target_include_directories(opstack-executor-block-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
    ${CMAKE_SOURCE_DIR}/bcos-rpc
    ${CMAKE_SOURCE_DIR}/bcos-evm/test/opstack)
target_compile_definitions(opstack-executor-block-tests PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/golden/engine"
    OP_DA_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json")
# Mirrors engine/test/CMakeLists.txt:47-48. Without this the provenance test's
# "corpus absent" branch skips in CI as well, and the M2 envelope baseline
# degenerates into "no cells checked" while staying green.
if(REQUIRE_T8N_CORPUS OR DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{CI})
    target_compile_definitions(opstack-executor-block-tests PRIVATE FISCO_REQUIRE_T8N_CORPUS=1)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(opstack-executor-block-tests PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

add_custom_target(op-karst-release-gate
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-op-karst-release-gate.sh
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/opstack-executor-block-tests --run_test=OpKarstReleaseGateSuite
    DEPENDS opstack-executor-block-tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME OpKarstReleaseGate
    COMMAND bash -c
    "${CMAKE_SOURCE_DIR}/tools/check-op-karst-release-gate.sh && \
     ${CMAKE_CURRENT_BINARY_DIR}/opstack-executor-block-tests --run_test=OpKarstReleaseGateSuite")

add_executable(opstack-executor-detail-tests
    TestMain.cpp
    OpMismatchedFieldTest.cpp
)
set_target_properties(opstack-executor-detail-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-executor-detail-tests PRIVATE
    opstack-executor
    codec
    evmone::evmone
    intx::intx
    Boost::unit_test_framework
    bcos-utilities
    protocol-tars
    blst
)
add_test(NAME OpstackExecutorDetailTests COMMAND opstack-executor-detail-tests)

# DA/operator-fee matrix runner (op-alignment line).
add_executable(opstack-da-matrix-runner da-matrix/run_fisco.cpp)
set_target_properties(opstack-da-matrix-runner PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-da-matrix-runner PRIVATE
    opstack-executor
    bcosevm::opstack
    jsoncpp_static
    evmone::evmone
    intx::intx
    bcos-utilities
    blst
)
add_test(NAME DaMatrixFiscoBaseline COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --out ${CMAKE_CURRENT_BINARY_DIR}/out_fisco.json)
add_test(NAME DaMatrixFiscoCheckOpgeth COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check opgeth
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/opgeth)
add_test(NAME DaMatrixFiscoCheckOpRevm COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check oprevm
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/oprevm)
