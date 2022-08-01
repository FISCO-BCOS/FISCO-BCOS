file(GLOB_RECURSE SOURCES main.cpp testExecutorManager.cpp testKeyLocks.cpp testScheduler.cpp testChecksumAddress.cpp testDmcStepRecorder.cpp testExecutivePool.cpp testDmcExecutor.cpp testSchedulerImpl.cpp)

# cmake settings
set(TEST_BINARY_NAME bcos-dispatcher-test)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR}/bcos-scheduler/src)
target_compile_options(${TEST_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-variable)

find_package(Boost REQUIRED unit_test_framework serialization)

target_link_libraries(${TEST_BINARY_NAME} ${SCHEDULER_TARGET} ${CRYPTO_TARGET}
Boost::unit_test_framework ${TARS_PROTOCOL_TARGET} ${PBPROTOCOL_TARGET} Boost::serialization ${STORAGE_TARGET} ${SECURITY_TARGET}) 

add_test(NAME test-scheduler WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
