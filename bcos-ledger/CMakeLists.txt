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


find_package(Boost REQUIRED serialization)

file(GLOB_RECURSE SRCS bcos-ledger/*.cpp)
add_library(ledger ${SRCS})
target_include_directories(ledger PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-ledger>)
target_link_libraries(ledger PUBLIC tool protocol-tars codec table bcos-concepts bcos-crypto Boost::serialization)
# bcos-storage/KeyPrefixes.h (the "/mpt/" node-row key layout) is header-only; a full
# `storage` link would pull RocksDB into the exported ledger target, so only the include
# directory is added (build tree only, not exported).
target_include_directories(ledger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../bcos-storage)
set_target_properties(ledger PROPERTIES UNITY_BUILD "ON")

# test related
if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS ledger EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-ledger" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
