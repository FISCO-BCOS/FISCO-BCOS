find_package(Boost REQUIRED unit_test_framework)
find_package(evmone REQUIRED)
# rlp_encode.cpp left the production lib (zero callers); its only consumer, OpReceiptEncodeTest,
# ADL-calls evmone::state::rlp_encode. Re-derive EVMONE_TEST_UTILS_DIR here — set only in sibling bcos-evm scope.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in the TU.
add_executable(opstack-executor-tests OpstackExecutorTest.cpp)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor           # INTERFACE: pulls ethereum-executor + bcos-evm-opstack + bcos-evm-eth + bcos-framework
    protocol-tars              # TransactionImpl / BlockHeaderImpl / TransactionReceiptFactoryImpl
    bcos-crypto                # CryptoSuite / Keccak256
    codec                      # rlp::decode for the web3 tx fixture
    rpc                        # bcos::rpc::Web3Transaction (model .cpp)
    evmone::evmone
    Boost::unit_test_framework)
# Web3Transaction + tars aggregate init are not unity-build friendly here.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD OFF)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

find_package(jsoncpp CONFIG REQUIRED)

# evmone_precompiles' interface adds a bare `-lblst`; with the shared-vcpkg worktree layout the
# vcpkg lib dir is not on the linker search path, so resolve the library to a full path here.
find_library(BLST_LIBRARY NAMES blst REQUIRED)
get_filename_component(BLST_LIB_DIR ${BLST_LIBRARY} DIRECTORY)

add_executable(opstack-executor-block-tests
    TestMain.cpp
    OpT8nReplayTest.cpp
    OpReceiptEncodeTest.cpp
    OpL1EdgeGateTest.cpp
    OpNewPayloadRpcE2eTest.cpp
    OpSchedulerSeamSmokeTest.cpp
    OpEngineBranchSmokeTest.cpp
    OpSchedulerTest.cpp
    OpBlockInjectorTest.cpp
    OpDualPathEquivalenceTest.cpp
    Storage2StatePoisonTest.cpp
    # op-alignment line test additions ported across (adapted to the refactored headers)
    OpL1BlockDepositTest.cpp
    OpDaMatrixSchemaTest.cpp
    OpJovianShapeTest.cpp
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
    codec protocol-tars ledger engine bcos-utilities jsoncpp_static rpc
    transaction-scheduler)  # OpSchedulerTest: SchedulerSkeleton::current() (BaselineScheduler.cpp)
# evmone_precompiles' bare -lblst needs the vcpkg lib dir on the search path (shared-vcpkg
# worktree layout does not put it in the implicit link directories).
target_link_directories(opstack-executor-block-tests PRIVATE ${BLST_LIB_DIR})
target_include_directories(opstack-executor-block-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
    ${CMAKE_SOURCE_DIR}/bcos-rpc)
target_compile_definitions(opstack-executor-block-tests PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/golden/engine"
    OP_DA_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json")
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

# Extracted pure-function layer (OpRlpDecode.h / Storage2StateHelpers.h): direct unit tests
# over the decode primitives and state-bridge helpers, no class-template instantiation.
add_executable(opstack-executor-detail-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    Storage2StateHelpersTest.cpp
    OpMismatchedFieldTest.cpp
)
set_target_properties(opstack-executor-detail-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-executor-detail-tests PRIVATE
    opstack-executor codec evmone::evmone intx::intx Boost::unit_test_framework bcos-utilities protocol-tars)
target_link_directories(opstack-executor-detail-tests PRIVATE ${BLST_LIB_DIR})  # bare -lblst (see above)
add_test(NAME OpstackExecutorDetailTests COMMAND opstack-executor-detail-tests)

# DA/operator-fee matrix runner (op-alignment line). Consumes da-matrix/da_matrix.json,
# computes L1 data fee + charged operator fee per case, writes out_fisco.json; --check mode
# compares value-level against the committed op-geth / op-revm golden snapshots
# (known_divergence rows skipped+counted; zero-compare run fails).
add_executable(opstack-da-matrix-runner da-matrix/run_fisco.cpp)
set_target_properties(opstack-da-matrix-runner PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-da-matrix-runner PRIVATE
    opstack-executor bcosevm::opstack jsoncpp_static evmone::evmone intx::intx bcos-utilities)
target_link_directories(opstack-da-matrix-runner PRIVATE ${BLST_LIB_DIR})  # bare -lblst (see above)
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
