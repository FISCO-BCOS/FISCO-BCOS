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


project(bcos-tool VERSION ${VERSION})

find_package(Boost REQUIRED COMPONENTS serialization)
find_package(jsoncpp CONFIG REQUIRED)
find_package(tarscpp REQUIRED)
find_package(TBB REQUIRED)

file(GLOB SRCS bcos-tool/*.cpp)
add_library(${TOOL_TARGET} ${SRCS})
target_include_directories(${TOOL_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/bcos-protocol>
)
target_link_libraries(${TOOL_TARGET} PUBLIC ${TABLE_TARGET} ${UTILITIES_TARGET} bcos-framework jsoncpp_static tarscpp::tarsservant tarscpp::tarsutil TBB::tbb)
set_target_properties(${TOOL_TARGET} PROPERTIES UNITY_BUILD "ON")

if(TESTS)
  enable_testing()
  set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
  add_subdirectory(test)
endif()

# for code coverage
#if (COVERAGE)
#    include(Coverage)
#    config_coverage("tool-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
#endif ()

include(GNUInstallDirs)
install(TARGETS ${TOOL_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")