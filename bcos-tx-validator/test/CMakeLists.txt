#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-tx-validator
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")

set(TEST_BINARY_NAME test-bcos-tx-validator)

find_package(Boost REQUIRED unit_test_framework)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})
target_link_libraries(${TEST_BINARY_NAME} bcos-tx-validator Boost::unit_test_framework)
add_test(NAME ${TEST_BINARY_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
