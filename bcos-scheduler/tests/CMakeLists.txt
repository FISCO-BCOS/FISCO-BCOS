file(GLOB_RECURSE SOURCES main.cpp testExecutorManager.cpp testKeyLocks.cpp testScheduler.cpp testChecksumAddress.cpp)

# cmake settings
include(SearchTestCases)
set(TEST_BINARY_NAME bcos-dispatcher-test)
config_test_cases("" "${SOURCES}" bin/${TEST_BINARY_NAME} "")

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})
target_compile_options(${TEST_BINARY_NAME} PRIVATE)

hunter_add_package(wedpr-crypto)
hunter_add_package(bcos-tars-protocol)
hunter_add_package(tarscpp)
find_package(wedpr-crypto CONFIG QUIET REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED unit_test_framework serialization)
find_package(bcos-tars-protocol CONFIG QUIET REQUIRED)
find_package(tarscpp CONFIG QUIET REQUIRED)
target_link_libraries(${TEST_BINARY_NAME} ${BCOS_DISPATCHER_TARGET} scheduler bcos-framework::storage bcos-framework::protocol-pb bcos-framework::codec wedpr-crypto::crypto
Boost::unit_test_framework bcos-tars-protocol::protocol-tars TBB::tbb)
