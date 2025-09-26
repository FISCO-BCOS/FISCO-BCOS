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
# cmake settings
set(TEST_BINARY_NAME test-bcos-framework)
include(SearchTestCases)
set(SOURCES unittests/storage2/TestMemoryStorage.cpp
    unittests/storage/TestCondition.cpp
    unittests/main/main.cpp
    unittests/interfaces/ExecutorTest.cpp
    unittests/interfaces/FeaturesTest.cpp
    unittests/interfaces/TestTransactionExecutor.cpp)

add_executable(${TEST_BINARY_NAME} ${SOURCES})

find_package(Boost REQUIRED serialization unit_test_framework)
target_link_libraries(${TEST_BINARY_NAME} PUBLIC ${UTILITIES_TARGET} bcos-framework Boost::serialization Boost::unit_test_framework)
set_source_files_properties("unittests/main/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
# add_test(NAME test-framework WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})
config_test_cases("" "${SOURCES}" "${TEST_BINARY_NAME}" "" "")