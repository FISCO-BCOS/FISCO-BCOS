#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-protocol
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
file(GLOB_RECURSE SOURCES "*.cpp" "*.h" "*.sol")
# cmake settings
# include(SearchTestCases)
set(TEST_BINARY_NAME test-bcos-protocol)
config_test_cases("" "${SOURCES}" bin/${TEST_BINARY_NAME} "")

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ${CMAKE_SOURCE_DIR} ${BCOS_TEST_DIR})

find_package(wedpr-crypto CONFIG QUIET REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} ${PROTOCOL_TARGET} ${PBPROTOCOL_TARGET} ${UTILITIES_TARGET} ${CRYPTO_TARGET} Boost::unit_test_framework)