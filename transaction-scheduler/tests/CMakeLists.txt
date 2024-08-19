file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(test-transaction-scheduler ${SOURCES})
find_package(Boost REQUIRED serialization unit_test_framework)

target_link_libraries(test-transaction-scheduler transaction-scheduler ${EXECUTOR_TARGET} ${TARS_PROTOCOL_TARGET} ${TABLE_TARGET} bcos-framework Boost::unit_test_framework)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-transaction-scheduler PROPERTIES UNITY_BUILD "ON")
add_test(NAME test-transaction-scheduler WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND test-transaction-scheduler)
