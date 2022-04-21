#------------------------------------------------------------------------------
# Top-level CMake file for bcos-security
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-security
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
project(bcos-security VERSION ${VERSION})

aux_source_directory(./bcos-security SRC_LIST)
include_directories(./bcos-security)
add_library(${SECURITY_TARGET} ${SRC_LIST})

target_link_libraries(${SECURITY_TARGET} PUBLIC ${UTILITIES_TARGET} ${TOOL_TARGET})