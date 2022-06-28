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
list(APPEND SOURCES "TestRocksDBStorage.cpp" "main.cpp")
# cmake settings
set(TEST_BINARY_NAME test-bcos-storage)

if(TIKV)
    list(APPEND SOURCES "TestTiKVStorage.cpp")
else()
    set(EXCLUDE_SUITES "TestTiKVStorage")
endif()

# config_test_cases("" "${SOURCES}" bin/${TEST_BINARY_NAME} "${EXCLUDE_SUITES}")

add_executable(${TEST_BINARY_NAME} ${SOURCES})
target_include_directories(${TEST_BINARY_NAME} PRIVATE . ../../src)

find_package(Boost REQUIRED unit_test_framework log)

target_link_libraries(${TEST_BINARY_NAME} Boost::unit_test_framework Boost::log ${STORAGE_TARGET} ${SECURITY_TARGET})
add_test(NAME test-storage WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${TEST_BINARY_NAME})

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("storage-coverage" "'/usr*' 'boost/**'")
endif ()
