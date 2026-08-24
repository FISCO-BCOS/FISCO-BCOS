#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-mempool
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-mempool)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC mempool bcos-crypto protocol-tars Boost::unit_test_framework)
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")