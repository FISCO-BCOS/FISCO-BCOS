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
# Unity batches pull range-v3 and libc++ std::ranges into the same TU on newer SDKs.
# UNITY_BUILD is OFF for the whole target, which subsumes release-3.17.0's per-file
# SKIP_UNITY_BUILD_INCLUSION opt-outs for DAGTest / DagTransferPrecompiledTest /
# SmallBankPrecompiledTest -- those files are already compiled standalone here.
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "OFF")
# CompatStaticGuardsTest locates repo sources from __FILE__. With UNITY_BUILD off the compiler
# sees a path relative to its own working directory, while ctest runs the binary from the build
# root, so the relative form resolves to the wrong place. Give the test an absolute anchor.
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE FISCO_REPO_ROOT="${CMAKE_SOURCE_DIR}")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "${EXCLUDE_SUITES}" "")
