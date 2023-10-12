#------------------------------------------------------------------------------
# Top-level CMake file for bcos-sealer
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-sealer
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

project(bcos-sealer VERSION ${VERSION})

aux_source_directory(./bcos-sealer SRC_LIST)
include_directories(./bcos-sealer)
add_library(${SEALER_TARGET} ${SRC_LIST})

find_package(tarscpp REQUIRED)

target_link_libraries(${SEALER_TARGET} PUBLIC ${TOOL_TARGET} ${UTILITIES_TARGET} bcos-framework ${TXPOOL_TARGET} ${TARS_PROTOCOL_TARGET})

if(TESTS)
  enable_testing()
  set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
  add_subdirectory(test)
endif()
