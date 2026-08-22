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
# headers) across all test files — measured on GCC 14/Debug: test target rebuild
# 150.7s -> ~90s wall. No per-file opt-outs remain:
#  - the global `using namespace std;` files were fixed to targeted `using std::x;`
#    (a global using-directive makes range-v3's ::ranges ambiguous with ::std::ranges
#    inside unity TUs);
#  - BOOST_AUTO_TEST_SUITE(Compat) shared by evmone/compat/Compat*.cpp works fine in
#    unity TUs with this Boost version (registration symbols are uniquified);
#  - the release-3.17.0-era opt-outs (DAGTest / DagTransferPrecompiledTest /
#    SmallBankPrecompiledTest) were verified safe on GCC 14.
# Default batch size (8): measured GCC 14/Debug on a 20-core box — test target clean
# rebuild ~150s -> ~120-145s wall, ~2200s -> ~1500-1800s CPU; batch 4 was strictly
# worse (more repeated header parsing, no wall-clock gain).
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
# CompatStaticGuardsTest locates repo sources from __FILE__. With UNITY_BUILD off the compiler
# sees a path relative to its own working directory, while ctest runs the binary from the build
# root, so the relative form resolves to the wrong place. Give the test an absolute anchor.
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE FISCO_REPO_ROOT="${CMAKE_SOURCE_DIR}")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "${EXCLUDE_SUITES}" "")
