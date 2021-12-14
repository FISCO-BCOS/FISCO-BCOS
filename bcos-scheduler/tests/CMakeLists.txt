file(GLOB_RECURSE SOURCES main.cpp testExecutorManager.cpp testKeyLocks.cpp testScheduler.cpp testChecksumAddress.cpp)

# cmake settings
# include(SearchTestCases)
set(TEST_BINARY_NAME bcos-dispatcher-test)
# config_test_cases("" "${SOURCES}" bin/${TEST_BINARY_NAME} "")

include_directories(${CMAKE_SOURCE_DIR}/bcos-scheduler)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})
target_compile_options(${TEST_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-variable)

hunter_add_package(wedpr-crypto)

find_package(wedpr-crypto CONFIG QUIET REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED unit_test_framework serialization)
find_package(tarscpp CONFIG QUIET REQUIRED)

target_link_libraries(${TEST_BINARY_NAME} scheduler wedpr-crypto::crypto
Boost::unit_test_framework protocol-tars)
add_test(NAME test-scheduler WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})