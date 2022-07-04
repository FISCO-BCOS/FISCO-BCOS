cmake_minimum_required(VERSION 3.20)

project(test-lightnode)

find_package(Boost REQUIRED unit_test_framework)

add_executable(test-lightnode LedgerImplTest.cpp main.cpp)
target_compile_options(test-lightnode PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic -ggdb3 -O0)
target_link_libraries(test-lightnode PUBLIC ${TARS_PROTOCOL_TARGET} Boost::unit_test_framework)

add_test(NAME test-lightnode COMMAND lightnode-test)