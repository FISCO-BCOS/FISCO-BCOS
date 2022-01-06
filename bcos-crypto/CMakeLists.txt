#------------------------------------------------------------------------------
# Top-level CMake file for bcos-crypto
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-crypto
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

project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE SRCS encrypt/*.cpp hash/*.cpp signature/*.cpp)

hunter_add_package(wedpr-crypto)
find_package(wedpr-crypto CONFIG REQUIRED)

add_library(${CRYPTO_TARGET} ${SRCS})
target_link_libraries(${CRYPTO_TARGET} PUBLIC ${UTILITIES_TARGET} wedpr-crypto::crypto wedpr-crypto::extend-crypto)

option(TOOLS "compile demo or not" OFF)

if(TOOLS)
add_subdirectory(demo)
endif()

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
