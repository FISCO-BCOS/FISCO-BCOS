#------------------------------------------------------------------------------
# Top-level CMake file for bcos-tx-validator
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
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
project(bcos-tx-validator VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST bcos-tx-validator/*.cpp)

add_library(bcos-tx-validator ${SRC_LIST})
target_include_directories(bcos-tx-validator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-tx-validator>)

# rlp-protocol decodes the signed envelope; protocol-tars is where the mirror lives and is the
# only way to write it -- protocol::Transaction exposes no setter for a business field, by
# design (it is an immutable-after-signing value object). Deliberately NOT linking either
# transaction pool: this module is stateless and neither pool's public interface changes.
# `ledger` supplies getSystemConfig and LedgerMethods; intx::intx is propagated by
# bcos-framework for TxGasModel.h's 512-bit balance arithmetic.
target_link_libraries(bcos-tx-validator PUBLIC
    bcos-framework bcos-protocol bcos-crypto ledger rlp-protocol protocol-tars)
set_target_properties(bcos-tx-validator PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-tx-validator EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-tx-validator" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
