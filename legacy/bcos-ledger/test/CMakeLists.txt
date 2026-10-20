# Legacy LedgerImpl test
set(TEST_BINARY_NAME test-legacy-ledger)

file(GLOB SOURCES "*.cpp")
add_executable(${TEST_BINARY_NAME} ${SOURCES})
# main.cpp defines BOOST_TEST_MODULE; merged into a unity batch the boost.test
# entry macro is disabled (headers already included), breaking the link with
# undefined init_unit_test_suite.
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE _TESTS_)
target_include_directories(${TEST_BINARY_NAME} PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(${TEST_BINARY_NAME} PRIVATE ${CMAKE_BINARY_DIR}/bcos-framework)

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} PRIVATE ledger bcos-crypto protocol-tars Boost::unit_test_framework bcos-legacy)

add_test(NAME ${TEST_BINARY_NAME} COMMAND ${TEST_BINARY_NAME})
