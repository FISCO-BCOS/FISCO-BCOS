#------------------------------------------------------------------------------
# Top-level CMake file for bcos-boostssl
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-boostssl
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

project(bcos-boostssl VERSION "3.0.0")
set(VERSION_SUFFIX "-rc2")

find_package(Boost CONFIG REQUIRED log chrono system filesystem thread iostreams)
find_package(OpenSSL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

# include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-boostssl)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(bcos-boostssl/context SRCS)
aux_source_directory(bcos-boostssl/httpserver SRCS)
aux_source_directory(bcos-boostssl/network SRCS)
aux_source_directory(bcos-boostssl/utilities SRCS)
aux_source_directory(bcos-boostssl/websocket SRCS)

add_library(boostssl ${SRCS})
target_link_libraries(boostssl Boost::system Boost::filesystem Boost::system
Boost::log Boost::chrono Boost::iostreams Boost::thread OpenSSL::SSL OpenSSL::Crypto)

# TODO: remove ut temp for remove bcos-framework deps
# ut
# if (TESTS)
#    enable_testing()
#    set(CTEST_OUTPUT_ON_FAILURE TRUE)
#    add_subdirectory(test)
# endif()

# if (BUILD_EXEC_TEST)
#     add_subdirectory(test/exec)
# endif()

# for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("boostssl-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_SOURCE_DIR}/test/mock**' '${CMAKE_SOURCE_DIR}/test/main**'")
# endif ()
