cmake_minimum_required(VERSION 3.15)

project(Tars-Service-Test)

file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(fisco-bcos-test ${SOURCES})
target_compile_options(fisco-bcos-test PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic -ggdb3)
find_package(Boost CONFIG QUIET REQUIRED unit_test_framework program_options)
target_link_libraries(fisco-bcos-test framework ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} Boost::program_options Boost::unit_test_framework)

add_test(NAME tars-test WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND fisco-bcos-test)