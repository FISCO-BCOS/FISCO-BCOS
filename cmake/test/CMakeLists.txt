#------------------------------------------------------------------------------
# Top-level CMake file for bcos-cmake-scripts.
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
#------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)
list(APPEND CMAKE_MODULE_PATH ../)
set(BCOS_DOXYGEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../")

#init hunter
include(HunterGate)
HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/v0.23.295.tar.gz"
    SHA1 "8a3447594fa5948cc7c6888dd86a9e823259c69f"
)
project(bcos-cmake-scripts-test VERSION "3.0")
# basic settings
include(Options)
configure_project()
include(CompilerSettings)
include(BuildDocs)

include(Coverage)
config_coverage("test_cov" "")
