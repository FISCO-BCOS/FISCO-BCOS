#------------------------------------------------------------------------------
# Top-level CMake file for bcos-tool
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-tool
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
project(bcos-tool VERSION ${VERSION})

find_package(Boost REQUIRED COMPONENTS serialization)
find_package(jsoncpp CONFIG REQUIRED)
find_package(tarscpp REQUIRED)
find_package(TBB REQUIRED)

file(GLOB SRCS *.cpp)
add_library(${TOOL_TARGET} ${SRCS})
target_include_directories(${TOOL_TARGET} PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/bcos-protocol>
)
target_link_libraries(${TOOL_TARGET} PUBLIC ${UTILITIES_TARGET} bcos-framework bcos-concepts jsoncpp_static tarscpp::tarsservant tarscpp::tarsutil TBB::tbb)

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tool-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()

include(GNUInstallDirs)
install(TARGETS ${TOOL_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")