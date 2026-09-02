#------------------------------------------------------------------------------
# Unit tests for bcos-single-consensus
#------------------------------------------------------------------------------

file(GLOB_RECURSE SOURCES "unittests/*.cpp")
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-single-consensus)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC single-consensus Boost::unit_test_framework)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
