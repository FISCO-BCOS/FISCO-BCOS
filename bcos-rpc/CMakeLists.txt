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

include(Version)
project(bcos-rpc VERSION ${VERSION})

find_package(jsoncpp CONFIG REQUIRED)
find_package(tarscpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB_RECURSE SRCS bcos-rpc/*.cpp)

add_library(${RPC_TARGET} ${SRCS} ${HEADERS})
target_compile_options(${RPC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(${RPC_TARGET} PUBLIC bcos-boostssl OpenSSL::SSL OpenSSL::Crypto ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} jsoncpp_lib_static ${CRYPTO_TARGET})

#if (TESTS)
#    enable_testing()
#    set(CTEST_OUTPUT_ON_FAILURE TRUE)
#    add_subdirectory(test)
#endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("rpc-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
endif ()
