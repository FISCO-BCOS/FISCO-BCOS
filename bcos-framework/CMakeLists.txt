#------------------------------------------------------------------------------
# Top-level CMake file for bcos-framework
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


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework)

find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(TBB REQUIRED)

# LedgerConfig.h includes <evmc/evmc.hpp> — evmone installs the evmc headers. Header-only usage:
# the seven framework sources reference no evmone symbol, and linking evmone on a STATIC target
# would land libevmone.a on every consumer's link line regardless of PUBLIC/PRIVATE — so no
# evmone find_package/link at all; the include dirs propagate PUBLIC-ly from evmone's interface
# (a generate-time genex; the target is created by the root's find_package(evmone) in FULLNODE
# builds, which also creates blst via its config's find_dependency(blst) — later calls no-op).
set(BCOS_FRAMEWORK_SOURCES
    bcos-framework/ledger/Features.cpp
    bcos-framework/ledger/L2ConfigLoader.cpp
    bcos-framework/multigroup/ChainNodeInfo.cpp
    bcos-framework/multigroup/GroupInfo.cpp
    bcos-framework/protocol/Protocol.cpp
    bcos-framework/protocol/Transaction.cpp
    bcos-framework/storage/Entry.cpp)

add_library(bcos-framework STATIC ${BCOS_FRAMEWORK_SOURCES})
if(FULLNODE)
    target_include_directories(bcos-framework PUBLIC
        $<TARGET_PROPERTY:evmone::evmone,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_include_directories(bcos-framework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-framework>)
target_link_libraries(bcos-framework PUBLIC
    bcos-task
    bcos-utilities
    bcos-crypto
    Microsoft.GSL::GSL
    fmt::fmt
    TBB::tbb)
set_target_properties(bcos-framework PROPERTIES UNITY_BUILD "ON")
# Entry.cpp uses namespace bcos::storage which conflicts with unity build
# namespace stacking from other translation units in the same batch.
set_source_files_properties(bcos-framework/storage/Entry.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-framework EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-framework" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
