cmake_minimum_required(VERSION 3.14)

find_package(fmt REQUIRED)

add_library(transaction-scheduler bcos-transaction-scheduler/BaselineScheduler.cpp)
target_include_directories(transaction-scheduler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-transaction-scheduler>)
target_link_libraries(transaction-scheduler PUBLIC bcos-framework fmt::fmt-header-only)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmark)
endif()