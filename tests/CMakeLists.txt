cmake_minimum_required(VERSION 3.14)

project(Tars-Service-Test)

file(GLOB_RECURSE SOURCES "unittest/*.cpp")

find_package(OpenMP 4.5 REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_executable(fisco-bcos-test ${SOURCES})
target_compile_options(fisco-bcos-test PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic -ggdb3 -O0)
find_package(Boost REQUIRED unit_test_framework program_options)
target_link_libraries(fisco-bcos-test PUBLIC ${TOOL_TARGET} ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} Boost::program_options Boost::unit_test_framework OpenMP::OpenMP_CXX TBB::tbb)

add_test(NAME tars-test WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND fisco-bcos-test)

add_subdirectory(perf)
add_subdirectory(preview)