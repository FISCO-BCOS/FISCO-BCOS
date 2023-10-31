cmake_minimum_required(VERSION 3.14)

project(FISCO-BCOS-Test)

file(GLOB_RECURSE SOURCES "unittest/*.cpp")

find_package(TBB CONFIG REQUIRED)

add_executable(fisco-bcos-test ${SOURCES})
find_package(Boost REQUIRED unit_test_framework program_options)
target_link_libraries(fisco-bcos-test PUBLIC ${TOOL_TARGET} ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} Boost::program_options Boost::unit_test_framework TBB::tbb)

# add_test(NAME fisco-bcos-test WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND fisco-bcos-test)

add_subdirectory(perf)