file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(test-transaction-scheduler ${SOURCES})
find_package(Boost REQUIRED serialization unit_test_framework)
find_package(FakeIt CONFIG REQUIRED)

target_link_libraries(test-transaction-scheduler
	transaction-executor
	transaction-scheduler
	executor
	ledger
	protocol-tars
	table
	bcos-framework
	Boost::unit_test_framework
	FakeIt::FakeIt-boost
)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# FullChainFixture.h pulls RocksDB and testutils headers whose names collide with the
# using-directives other unity-merged TUs carry; compile its users standalone.
set_source_files_properties(
    "FullChainFixtureSmokeTest.cpp" "BaselineSchedulerMPTL1UpgradeTest.cpp"
    "BaselineSchedulerMPTGenesisTest.cpp" "MPTFirstTouchLatencyBench.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-transaction-scheduler PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" "test-transaction-scheduler" "" "")

# Acceptance entries (ctest -L acceptance): the same Boost suites at FULL scale with
# MPT_ACCEPTANCE=1 in the environment. The per-case registrations config_test_cases made
# above stay default-runnable REDUCED-scale measurement passes (no env var -> no hard
# latency assertion), so the per-PR CI ctest run is never gated on wall-clock numbers.
#
# OFF by default ON PURPOSE: a plain `ctest` run executes every registered test regardless
# of labels, so registering these unconditionally would put the armed full-scale gates into
# every per-PR CI matrix job (including slow shared runners) — exactly the flaky-red this
# split exists to prevent. Release-verification build dirs configure with
# -DMPT_ACCEPTANCE_GATES=ON; tools/mpt_acceptance_gate.sh fails loudly (ctest
# --no-tests=error) when pointed at a build dir configured without it.
option(MPT_ACCEPTANCE_GATES
    "Register the full-scale MPT acceptance latency gates as ctest entries" OFF)
if(MPT_ACCEPTANCE_GATES)
    add_test(NAME acceptance/MPTFirstTouchLatencyBench WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMAND test-transaction-scheduler -t MPTFirstTouchLatencyBenchSuite)
    set_tests_properties(acceptance/MPTFirstTouchLatencyBench PROPERTIES
        LABELS "acceptance;benchmark" ENVIRONMENT "MPT_ACCEPTANCE=1" TIMEOUT 3600)
endif()
