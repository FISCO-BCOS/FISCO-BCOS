#------------------------------------------------------------------------------
# Top-level CMake file for bcos-rpc
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-rpc
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

project(bcos-rpc VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

find_package(jsoncpp CONFIG REQUIRED)
find_package(tarscpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/bcos-rpc)
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-boostssl)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)
aux_source_directory(bcos-rpc/event SRC_LIST)
aux_source_directory(bcos-rpc/jsonrpc SRC_LIST)
aux_source_directory(bcos-rpc/jsonrpc/groupmgr SRC_LIST)
aux_source_directory(bcos-rpc/ws SRC_LIST)
aux_source_directory(bcos-rpc/amop SRC_LIST)

add_library(rpc ${SRC_LIST} ${HEADERS})
target_compile_options(rpc PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(rpc PUBLIC framework jsoncpp_lib_static tarscpp::tarsservant boostssl OpenSSL::SSL OpenSSL::Crypto tars-protocol crypto)

# if (TESTS)
#     enable_testing()
#     set(CTEST_OUTPUT_ON_FAILURE TRUE)
#     add_subdirectory(test)
# endif()

# # for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("rpc-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
# endif ()
