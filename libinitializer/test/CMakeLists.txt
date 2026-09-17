#------------------------------------------------------------------------------
# Top-level CMake file for ut of libinitializer
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
include(SearchTestCases)

set(TEST_BINARY_NAME test-bcos-initializer)

find_package(Boost REQUIRED unit_test_framework)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR})
target_link_libraries(${TEST_BINARY_NAME} init pbft_init gateway Boost::unit_test_framework)
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "" "")
