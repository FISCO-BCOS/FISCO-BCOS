file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(test-transaction-executor ${SOURCES})

find_package(Boost REQUIRED serialization unit_test_framework)

target_link_libraries(test-transaction-executor transaction-executor ${LEDGER_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework evmone Boost::unit_test_framework)
add_test(NAME test-transaction-executor WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND test-transaction-executor)