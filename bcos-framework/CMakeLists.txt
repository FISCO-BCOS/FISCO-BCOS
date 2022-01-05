#------------------------------------------------------------------------------
# Top-level CMake file for bcos-framework
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

cmake_minimum_required(VERSION 3.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

include(Version)
project(bcos-framework VERSION ${VERSION})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

file(GLOB_RECURSE SRCS libexecutor/*.cpp libstorage/*.cpp)

find_package(TBB CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED log chrono system filesystem iostreams thread)
find_package(Protobuf CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(OpenMP 4.5 REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework)

add_library(framework ${SRCS} ${MESSAGES_SRCS} ${TXS_SYNC_MSG})
target_link_libraries(framework PUBLIC ${UTILITIES_TARGET} protobuf::libprotobuf TBB::tbb jsoncpp_lib_static OpenMP::OpenMP_CXX)

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE True)
    add_subdirectory(test)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("framework-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
