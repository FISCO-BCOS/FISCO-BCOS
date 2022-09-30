cmake_minimum_required(VERSION 3.17)

find_package(Boost REQUIRED unit_test_framework)
find_package(TBB REQUIRED)

add_executable(test-task TaskTest.cpp main.cpp)
target_link_libraries(test-task PUBLIC bcos-task Boost::unit_test_framework TBB::tbb)

add_test(NAME test-task COMMAND test-task)