#------------------------------------------------------------------------------
# Top-level CMake file for ut of bcos-gateway
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
if(BUILD_INTEG_TESTS)
    file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h" "integtests/*.cpp" "integtests/*.h" "unittests/*.sol")
else()
    file(GLOB_RECURSE SOURCES "unittests/*.cpp" "unittests/*.h" "unittests/*.sol")
endif()

# cmake settings
set(TEST_BINARY_NAME test-bcos-gateway)

if (TOOLS)
    add_subdirectory(main)
endif()

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE .)

target_compile_options(${TEST_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-variable)

find_package(Boost REQUIRED unit_test_framework)

target_link_libraries(${TEST_BINARY_NAME} ${GATEWAY_TARGET} ${TARS_PROTOCOL_TARGET} ${FRONT_TARGET} bcos-crypto Boost::unit_test_framework)
# add_test(NAME test-gateway WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
add_test(NAME ${TEST_BINARY_NAME} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bcos-gateway/test/unittests COMMAND ${TEST_BINARY_NAME})