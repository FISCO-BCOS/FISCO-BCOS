#------------------------------------------------------------------------------
# Top-level CMake file for bcos-tars-protocol
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-tars-protocol
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
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

project(bcos-tars-protocol VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

hunter_add_package(tarscpp)
find_package(tarscpp CONFIG REQUIRED)

# for tars generator
set(TARS_HEADER_DIR ${CMAKE_BINARY_DIR}/bcos-tars-protocol/tars)

set(BCOS_TARS_PROTOCOL_TARGET "protocol-tars")
add_subdirectory(bcos-tars-protocol)

# if (TESTS)
#     # fetch bcos-test
#     enable_testing()
#     set(CTEST_OUTPUT_ON_FAILURE TRUE)
#     add_subdirectory(test)
# endif()

# # for doxygen
# include(BuildDocs)
# buildDoc(bcos-tars-protocol-doc)

# for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("tars-protocol-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
# endif ()
