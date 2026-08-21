find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

# opstack-executor-tests: single Boost.Test target (BOOST_TEST_MODULE in TestMain.cpp). All
# part-4 unit tests plus the part-5 scheduler tests live here. OpSchedulerTest drives a real
# bcos::ledger::Ledger commit hook and SchedulerSkeleton::current(), so the target links
# transaction-scheduler / ledger / engine in addition to the part-4 set.
#
# No `ledger` link was the part-4 rule (header-only usage + the ledger→bcos-crypto→wedprcrypto
# chain breaking libc++ typed catch binary-wide, bcos-rpc/test/CMakeLists.txt:29-34); part-5
# OpSchedulerTest instantiates bcos::ledger::Ledger for real, so ledger IS required here.
add_executable(opstack-executor-tests
    TestMain.cpp
    OpstackExecutorTest.cpp
    OpReceiptEncodeTest.cpp
    OpRlpDecodeTest.cpp
    OpDepositEnvelopeTest.cpp
    OpTxConvertTest.cpp
    SmokeCompileTest.cpp
    OpCommonTest.cpp
    OpCommitmentsTest.cpp
    RecentBlockHashesTest.cpp
    PreBlockOpStepsTest.cpp
    OpBlockProcessTest.cpp
    OpSchedulerSeamSmokeTest.cpp
    OpSchedulerTest.cpp
    OpEngineBranchSmokeTest.cpp
    OpBlockInjectorTest.cpp
    Storage2StateHelpersTest.cpp
    Storage2StatePoisonTest.cpp
    support/SeedPreStateTest.cpp
)
# OpReceiptEncodeTest diffs the FISCO receipt leaf encoding against evmone's independent
# evmone::state::rlp_encode (Log / TransactionReceipt overloads). bcos-evm deliberately keeps
# rlp_encode.cpp out of the production lib (zero production callers — see bcos-evm/CMakeLists.txt),
# so this test target compiles the package-shipped source itself; its <test/state/...> includes
# resolve through the pkg-include-shim dir that bcos-evm-eth exposes PUBLIC-ly (transitively on
# this target via opstack-executor → bcos-evm-opstack).
get_target_property(_evmone_include_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_include_dirs 0 _evmone_include_dir)
cmake_path(NORMAL_PATH _evmone_include_dir)
target_sources(opstack-executor-tests PRIVATE "${_evmone_include_dir}/test/utils/rlp_encode.cpp")
# Vendored upstream source: same warning exemption bcos-evm grants its ported files.
set_source_files_properties("${_evmone_include_dir}/test/utils/rlp_encode.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor
    rpc
    protocol-tars
    codec
    bcos-crypto
    bcos-task
    jsoncpp_static
    transaction-scheduler  # OpSchedulerTest: SchedulerSkeleton::current() (BaselineScheduler.cpp)
    ledger                 # OpSchedulerTest: real bcos::ledger::Ledger commit hook
    engine
    bcos-utilities
    Boost::unit_test_framework
)
target_include_directories(opstack-executor-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
    ${CMAKE_SOURCE_DIR}/bcos-rpc)
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD OFF)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)
