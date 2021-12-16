#------------------------------------------------------------------------------
# Top-level CMake file for bcos-txpool
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-txpool
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

project(bcos-txpool VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

file(GLOB_RECURSE SRCS bcos-txpool/*.cpp)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-txpool)
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_BINARY_DIR}/bcos-framework)

add_library(txpool ${SRCS})
set_target_properties(txpool PROPERTIES UNITY_BUILD ON)
target_compile_options(txpool PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(txpool PUBLIC framework)

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

# for doxygen
# include(BuildDocs)
# buildDoc(bcos-txpool-doc)

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("txpool-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
