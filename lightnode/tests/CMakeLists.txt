cmake_minimum_required(VERSION 3.17)

project(test-lightnode)

find_package(Boost REQUIRED unit_test_framework)

add_executable(test-lightnode TransactionPoolTest.cpp main.cpp)
target_link_libraries(test-lightnode PUBLIC bcos-lightnode table protocol-tars Boost::unit_test_framework)

add_test(NAME test-lightnode COMMAND test-lightnode)