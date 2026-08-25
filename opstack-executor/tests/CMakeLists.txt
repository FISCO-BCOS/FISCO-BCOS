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


# opstack-executor-runtime-tests (OpstackExecutorTest) is split out: it compiles
# Web3TxHandler.cpp (RPC model) — a cross-block dependency that would couple this
# PR to the RPC+shared PR. It returns with the web3-model decoupling (next PR).
