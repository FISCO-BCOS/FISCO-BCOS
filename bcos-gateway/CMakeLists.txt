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

include(Version)
project(bcos-gateway VERSION ${VERSION})

find_package(OpenSSL REQUIRED)
find_package(tarscpp CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

file(GLOB_RECURSE SRCS bcos-gateway/*.cpp)

add_library(${GATEWAY_TARGET} ${SRCS})
target_link_libraries(${GATEWAY_TARGET} PUBLIC ${PROTOCOL_TARGET} jsoncpp_lib_static OpenSSL::SSL OpenSSL::Crypto ${TARS_PROTOCOL_TARGET} Snappy)
target_compile_options(${GATEWAY_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

if (APPLE)
# target_compile_options(${GATEWAY_TARGET} PRIVATE -faligned-allocation)
endif()

# ut
if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

# for doxygen
# include(BuildDocs)

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("gateway-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
endif ()
