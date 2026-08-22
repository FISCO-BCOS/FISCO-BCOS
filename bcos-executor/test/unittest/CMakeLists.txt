set(EXCLUDE_SUITES "")
file(GLOB_RECURSE SOURCES "*.cpp" "*.h")

# cmake settings
set(TEST_BINARY_NAME test-bcos-executor)

add_executable(${TEST_BINARY_NAME} ${SOURCES} libprecompiled/PrecompiledCallTest.cpp)
target_include_directories(${TEST_BINARY_NAME} PRIVATE ./ ../../src ../../include)

find_package(Boost REQUIRED unit_test_framework)
find_package(TBB CONFIG REQUIRED)
include(SearchTestCases)

target_link_libraries(${TEST_BINARY_NAME} executor ledger bcos-crypto protocol-tars bcos-framework rpc Boost::serialization Boost::unit_test_framework)
target_compile_options(${TEST_BINARY_NAME} PRIVATE -fno-var-tracking -Wno-unused-parameter)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# Unity build is ON for this target (Linux/GCC): it amortizes the heavy shared
# include set (~200k preprocessed lines per TU: boost.test + framework + executor
# headers) across ~40 test files — measured on GCC 14/Debug: test target rebuild
# 150.7s -> 111.0s wall (-26%), ~2200s -> ~1500s CPU (-32%).
# Opt-outs:
#  - evmone/compat/Compat*.cpp: all share BOOST_AUTO_TEST_SUITE(Compat), which would
#    redefine the same suite registration symbols inside one unity TU.
#  - DAGTest / DagTransferPrecompiledTest / SmallBankPrecompiledTest: kept standalone
#    since release-3.17.0 (name collisions in unity TUs).
#  - files with a global `using namespace std;`: range-v3 headers first included
#    after that using-directive in the same unity TU see an ambiguous `ranges`
#    (::ranges vs ::std::ranges), so they must compile standalone.
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
file(GLOB COMPAT_SOURCES "evmone/compat/Compat*.cpp")
set_source_files_properties(${COMPAT_SOURCES}
    "DAGTest.cpp"
    "libprecompiled/DagTransferPrecompiledTest.cpp"
    "libprecompiled/SmallBankPrecompiledTest.cpp"
    "libexecutive/TestShardingSyncStorageWrapper.h.cpp"
    "libexecutor/TestScaleUtils.cpp"
    "libexecutor/TestEVMExecutor.cpp"
    "libexecutor/TestBlockContext.cpp"
    "libexecutor/TestExecutiveStackFlow.cpp"
    "libexecutor/TestLedgerCache.cpp"
    "libexecutor/TestClockCache.cpp"
    "libexecutor/TestExecutiveState.cpp"
    "libexecutor/TestTxDAG.cpp"
    "libexecutor/TestDagExecutor.cpp"
    "libexecutor/TestAbiReader.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# CompatStaticGuardsTest locates repo sources from __FILE__. With UNITY_BUILD off the compiler
# sees a path relative to its own working directory, while ctest runs the binary from the build
# root, so the relative form resolves to the wrong place. Give the test an absolute anchor.
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE FISCO_REPO_ROOT="${CMAKE_SOURCE_DIR}")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "${EXCLUDE_SUITES}" "")
