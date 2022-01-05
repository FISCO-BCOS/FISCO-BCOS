file(GLOB_RECURSE SOURCES main.cpp testExecutorManager.cpp testKeyLocks.cpp testScheduler.cpp testChecksumAddress.cpp)

# cmake settings
# include(SearchTestCases)
set(TEST_BINARY_NAME bcos-dispatcher-test)
# config_test_cases("" "${SOURCES}" bin/${TEST_BINARY_NAME} "")

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR}/bcos-scheduler/src)
target_compile_options(${TEST_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-variable)

hunter_add_package(wedpr-crypto)

find_package(wedpr-crypto CONFIG QUIET REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED unit_test_framework serialization)

target_link_libraries(${TEST_BINARY_NAME} ${SCHEDULER_TARGET} ${CRYPTO_TARGET}
Boost::unit_test_framework ${TARS_PROTOCOL_TARGET})
add_test(NAME test-scheduler WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
