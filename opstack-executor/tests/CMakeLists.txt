find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in
# TestMain.cpp (the single main for the whole suite — the earlier three-target split left the
# add_test entries dangling with no add_executable; this restores the wiring with one target).
#
# No `ledger` link: header-only usage only (Classify.h), and the ledger→bcos-crypto→wedprcrypto
# chain breaks libc++ typed catch binary-wide (bcos-rpc/test/CMakeLists.txt:29-34), so
# exception-type assertions rely on exact-type BOOST_CHECK_THROW.
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
)
# Unity build: header-heavy Boost.Test TUs share a large common preamble (catch.hpp, evmone,
# op-rlp headers). TestMain.cpp defines BOOST_TEST_MODULE and stays out of the batch.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD "ON")
set_source_files_properties(TestMain.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor
    jsoncpp_static
    Boost::unit_test_framework
)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)
