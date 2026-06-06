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



project(bcos-tool VERSION ${VERSION})

# bcos-tool has been merged into bcos-utilities.
# The source files now live under:
#   bcos-utilities/bcos-utilities/topology/  (TreeTopology, NodeTimeMaintenance)
#   bcos-utilities/bcos-utilities/data/      (VersionConverter)
#   bcos-utilities/bcos-utilities/exceptions/ (Exceptions)
#   bcos-utilities/bcos-utilities/config/    (NodeConfig, BfsFileFactory)
#
# The 'tool' target is kept as a compatibility alias that forwards to:
#   bcos-utilities + bcos-config

add_library(tool INTERFACE)
target_link_libraries(tool INTERFACE bcos-utilities bcos-config)
target_include_directories(tool INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/bcos-utilities>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/bcos-protocol>)

if(TESTS)
  enable_testing()
  set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
  add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS tool EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")