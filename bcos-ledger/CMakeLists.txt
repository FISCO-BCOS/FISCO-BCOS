#------------------------------------------------------------------------------
# Top-level CMake file for bcos-ledger
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

cmake_minimum_required(VERSION 3.10)

find_package(Boost REQUIRED serialization)

add_library(${LEDGER_TARGET} bcos-ledger/Ledger.cpp bcos-ledger/LedgerMethods.cpp)
target_include_directories(${LEDGER_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-ledger>)
target_link_libraries(${LEDGER_TARGET} PUBLIC ${TOOL_TARGET} ${CODEC_TARGET} ${TABLE_TARGET} bcos-concepts Boost::serialization)
set_target_properties(${LEDGER_TARGET} PROPERTIES UNITY_BUILD "ON")

# test related
if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS ${LEDGER_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-ledger" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
