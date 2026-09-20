find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)
find_package(evmone REQUIRED)

# Lightweight suite: FakeTransaction / header-only surfaces.
# This binary links ledger via opstack-executor's PUBLIC closure (OpBlockExecute.cpp needs
# the trie builders). The older "do not link ledger — wedprcrypto breaks libc++ typed catch
# binary-wide" instruction is false on this preset: BOOST_CHECK_THROW(..., OpConsensusError)
# is what CI runs. Receipt tests use the same typed catch (BOOST_CHECK_EXCEPTION).
#
# BOOST_TEST_MODULE lives in TestMain.cpp (the single main for this suite).
add_executable(opstack-executor-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    OpDepositEnvelopeTest.cpp
    OpTxConvertTest.cpp
    SmokeCompileTest.cpp
    OpSchedulerPolicyTest.cpp
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


# Receipts-root leaf encoding (encodeReceiptForRoot / sealOpBlock). Needs evmone's packaged RLP
# helpers and ledger's computeTrieRootVarKey; kept off the lightweight binary to isolate the
# larger protocol/crypto dependency closure. The repository-owned evmone port deliberately
# installs test/utils/rlp_encode.{hpp,cpp} (ports/evmone/portfile.cmake §4d), and its generated
# package config calls find_dependency(blst). Build the consensus golden-vector suite
# unconditionally so a broken package contract fails configure instead of silently losing tests.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _evmone_inc_dirs OR _evmone_inc_dirs MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "the repository evmone package must export INTERFACE_INCLUDE_DIRECTORIES")
endif()
# Find the first include dir that actually contains the vendored test utils, instead of
# assuming list order (list(GET ... 0) would misdiagnose a reordered include list).
set(EVMONE_TEST_UTILS_DIR)
foreach(_dir IN LISTS _evmone_inc_dirs)
    cmake_path(NORMAL_PATH _dir)
    if(EXISTS "${_dir}/test/utils/rlp_encode.cpp")
        set(EVMONE_TEST_UTILS_DIR "${_dir}/test/utils")
        break()
    endif()
endforeach()
if(NOT EVMONE_TEST_UTILS_DIR)
    message(FATAL_ERROR "opstack-executor receipt tests require evmone test-utils "
        "(rlp_encode.cpp not found under any of: ${_evmone_inc_dirs})")
endif()
if(NOT TARGET blst)
    message(FATAL_ERROR "opstack-executor receipt tests require the blst CMake target")
endif()
add_executable(opstack-executor-receipt-tests
    TestMain.cpp
    OpReceiptEncodeTest.cpp
    OpCallWritebackTest.cpp
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
# Same GNU downgrades as opstack-executor-tests: the OP templates instantiate in this TU too
# (OpCallWritebackTest / OpReceiptEncodeTest), and the library's flags are PRIVATE.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(opstack-executor-receipt-tests PRIVATE
        -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
endif()
target_include_directories(opstack-executor-receipt-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
)
add_test(NAME OpstackExecutorReceiptTests COMMAND opstack-executor-receipt-tests)

# Scheduler suite: real executeBlock / commitBlock / call / callAtBlock against OpScheduler<MLS>.
# Isolated from the lightweight binary (ledger / protocol-tars / wedprcrypto typed-catch).
# Call fixtures use a FakeCallTx (no Web3Transaction.cpp — that RLP bind is #5496).
# ReorgUndo codec cases return with the tip-reorg follow-up.
add_executable(opstack-executor-scheduler-tests
    TestMain.cpp
    OpSchedulerTest.cpp
)
set_target_properties(opstack-executor-scheduler-tests PROPERTIES UNITY_BUILD OFF)
target_include_directories(opstack-executor-scheduler-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
)
target_link_libraries(opstack-executor-scheduler-tests PRIVATE
    opstack-executor
    protocol-tars
    bcos-crypto
    codec
    ledger
    evmone::evmone
    Boost::unit_test_framework
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
