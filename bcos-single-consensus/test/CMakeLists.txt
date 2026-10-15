#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-single-consensus
#------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "*.cpp")
add_executable(test-single-consensus ${SOURCES})

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(test-single-consensus single-consensus bcos-framework Boost::unit_test_framework)
include(SearchTestCases)
config_test_cases("" "${SOURCES}" "test-single-consensus" "" "")
