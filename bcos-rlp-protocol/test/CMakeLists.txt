#------------------------------------------------------------------------------
# Test CMake file for bcos-rlp-protocol
# ------------------------------------------------------------------------------
# Copyright (C) 2024 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "*.cpp")

set(TEST_BINARY_NAME test-bcos-rlp-protocol)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} PRIVATE
    bcos-rlp-protocol
    codec
    bcos-crypto
    bcos-utilities
    Boost::unit_test_framework
)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
add_test(NAME test-rlp-protocol WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
