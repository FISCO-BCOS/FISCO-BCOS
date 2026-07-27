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


