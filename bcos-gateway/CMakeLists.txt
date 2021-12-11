#------------------------------------------------------------------------------
# Top-level CMake file for bcos-gateway
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-gateway
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
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")

project(bcos-gateway VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

find_package(OpenSSL REQUIRED)
find_package(tarscpp CONFIG REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-gateway)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)

aux_source_directory(bcos-gateway SRC_LIST)
aux_source_directory(bcos-gateway/libnetwork SRC_LIST)
aux_source_directory(bcos-gateway/libp2p SRC_LIST)
aux_source_directory(bcos-gateway/libamop SRC_LIST)

add_library(gateway ${SRC_LIST} ${HEADERS})
target_link_libraries(gateway PUBLIC framework jsoncpp_lib_static OpenSSL::SSL OpenSSL::Crypto protocol-tars tarscpp::tarsservant)

target_compile_options(gateway PRIVATE -Wno-error -Wno-unused-variable)
if (APPLE)
target_compile_options(gateway PRIVATE -faligned-allocation)
endif()


# ut
# if (TESTS)
#     hunter_add_package(bcos-front)
#     find_package(bcos-front CONFIG REQUIRED)

#     enable_testing()
#     set(CTEST_OUTPUT_ON_FAILURE TRUE)
#     add_subdirectory(test)
# endif()

# for doxygen
# include(BuildDocs)

# for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("gateway-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
# endif ()
