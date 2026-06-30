#------------------------------------------------------------------------------
# CMake file for ut of bcos-rlp-protocol
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# ------------------------------------------------------------------------------
file(GLOB_RECURSE TEST_SOURCES "*.cpp" "*.h")
file(GLOB_RECURSE LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../bcos-rlp-protocol/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../bcos-rlp-protocol/*.h")
set(TEST_BINARY_NAME test-bcos-rlp-protocol)

add_executable(${TEST_BINARY_NAME} ${TEST_SOURCES} ${LIB_SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../bcos-rlp-protocol)

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} codec Boost::unit_test_framework)
add_test(NAME test-rlp-protocol WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})