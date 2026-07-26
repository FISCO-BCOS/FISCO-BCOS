file(GLOB_RECURSE SOURCES "*.cpp")

# Exclude standalone eest-runner and old boost-based EEST test from boost-test target
list(FILTER SOURCES EXCLUDE REGEX "EESTStandalone\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "EESTFixtureTest\\.cpp$")
add_executable(test-transaction-executor ${SOURCES})

find_package(Boost REQUIRED serialization unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

target_link_libraries(test-transaction-executor transaction-executor executor ledger protocol-tars bcos-framework rpc evmone::evmone Boost::unit_test_framework jsoncpp_static)
# Ensure libexecutor.a is fully archived before linking (UNITY_BUILD static lib).
add_dependencies(test-transaction-executor executor)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Eip2929AccessStateTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Eip2929HelperTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Web3AccessListResolverTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-transaction-executor PROPERTIES UNITY_BUILD "ON")
# add_test(NAME test-transaction-executor WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND test-transaction-executor)
config_test_cases("" "${SOURCES}" "test-transaction-executor" "" "")

# ==============================================================================
# Standalone EEST runner (no Boost.Test dependency, uses TBB for parallelism)
# ==============================================================================
add_executable(eest-runner EESTStandalone.cpp)
target_link_libraries(eest-runner
    ethereum-executor bcos-evm-eth executor ledger protocol-tars bcos-framework rpc
    evmone::evmone jsoncpp_static TBB::tbb)
add_dependencies(eest-runner executor)
set_source_files_properties("EESTStandalone.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(eest-runner PROPERTIES UNITY_BUILD OFF)
# Install alongside test binary
get_target_property(EEST_RUNNER_OUTPUT test-transaction-executor RUNTIME_OUTPUT_DIRECTORY)
if(NOT EEST_RUNNER_OUTPUT)
    set(EEST_RUNNER_OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()
set_target_properties(eest-runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EEST_RUNNER_OUTPUT}")
