#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-framework
# ------------------------------------------------------------------------------
# Copyright (C) 2021 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "./*.cpp")
# cmake settings
set(TEST_BINARY_NAME test-sealer)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
# main.cpp defines BOOST_TEST_MODULE (the test runner entry point); keep it out of the
# unity batch per project convention (bcos-ledger/test/CMakeLists.txt).
set_source_files_properties(main.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ../bcos-sealer)

find_package(Boost REQUIRED unit_test_framework log)
find_package(fmt REQUIRED)

target_link_libraries(${TEST_BINARY_NAME} PUBLIC sealer pbft rpbft ledger protocol-tars Boost::unit_test_framework Boost::log fmt::fmt)
include(SearchTestCases)
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "${EXCLUDE_SUITES}" "")

