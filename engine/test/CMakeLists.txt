#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-engine
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")
# OpEngineServiceParityTest lives next to Eth parity for directory symmetry, but was written
# against sync-release (dual rawTransactions + GoldenSample/t8n). Exclude until adapted to
# release-3.18.0's single transactions[i].raw carrier.
list(FILTER SOURCES EXCLUDE REGEX "OpEngineServiceParityTest\\.cpp$")
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-engine)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC engine mempool protocol-tars rlp-protocol Boost::unit_test_framework)
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceParityTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(
    "unittests/engine/EthEngineServiceTransactionalTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")