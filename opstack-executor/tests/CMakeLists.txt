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
