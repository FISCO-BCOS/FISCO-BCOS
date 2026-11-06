#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-network
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
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
file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h")
include(SearchTestCases)

# cmake settings
set(TEST_BINARY_NAME test-bcos-network)

add_executable(${TEST_BINARY_NAME} ${SOURCES})
# main.cpp defines BOOST_TEST_MODULE; when merged into a unity batch, boost.test
# headers are already included by earlier files and the entry macro is disabled,
# leaving the linker without init_unit_test_suite.
set_source_files_properties("unittests/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_include_directories(${TEST_BINARY_NAME} PRIVATE .)

find_package(Boost REQUIRED unit_test_framework)

# The suite deliberately links ONLY the library under test (plus its PUBLIC dependencies):
# bcos-network is a standalone generic framed transport, so its tests must stay runnable in
# configurations where the gateway (or any other consumer) is not built.
target_link_libraries(${TEST_BINARY_NAME} bcos-network Boost::unit_test_framework)

config_test_cases("" "${SOURCES}" "${TEST_BINARY_NAME}" "" "")
