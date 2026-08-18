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
# LedgerConfig.h includes <evmc/evmc.hpp> (evmc::bytes32 / evmc_uint256be / evmc_revision).
# evmone::evmone is the vcpkg package that installs the evmc headers; its
# INTERFACE_INCLUDE_DIRECTORIES is what makes `#include <evmc/evmc.hpp>` resolve.
# Linux/macOS builds get the include path transitively through vcpkg's global
# -isystem; MSVC builds need the dependency declared on the target (C1083 otherwise).
# Fullnode-only: evmone lives in the vcpkg "fullnode" feature, which Options.cmake
# appends to VCPKG_MANIFEST_FEATURES only when FULLNODE is on, while this directory
# is added unconditionally by the root CMakeLists — an unguarded REQUIRED lookup here
# aborts the configure of FULLNODE=OFF / ONLY_CPP_SDK / lightnode builds, none of
# which compile anything that includes that header.
if(FULLNODE)
    find_package(evmone CONFIG REQUIRED)
    # evmone's config find_dependency(blst) creates the `blst` imported target in THIS
    # directory's scope (first creation; blst-config.cmake's `if(NOT TARGET blst)` +
    # include_guard(GLOBAL) make every later find_package(blst) — including the root's
    # own, even with the GLOBAL keyword — a no-op that creates nothing). IMPORTED_GLOBAL
    # may only be set from the creating directory, i.e. here, so promote it now or the
    # root-scope and sibling consumers (bcos-executor / transaction-executor /
    # fisco-bcos-air, all linked after this point) resolve the bare `blst` name to -lblst.
    find_package(blst CONFIG REQUIRED)
    set_target_properties(blst PROPERTIES IMPORTED_GLOBAL TRUE)
endif()

set(BCOS_FRAMEWORK_SOURCES
    bcos-framework/ledger/Features.cpp
    bcos-framework/ledger/L2ConfigLoader.cpp
    bcos-framework/multigroup/ChainNodeInfo.cpp
    bcos-framework/multigroup/GroupInfo.cpp
    bcos-framework/protocol/Protocol.cpp
    bcos-framework/protocol/Transaction.cpp
    bcos-framework/storage/Entry.cpp)

add_library(bcos-framework STATIC ${BCOS_FRAMEWORK_SOURCES})
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
if(FULLNODE)
    # PUBLIC rather than PRIVATE: LedgerConfig.h is an installed public header that
    # includes <evmc/evmc.hpp>, so every in-tree consumer compiling that header needs
    # the evmc include path propagated (MSVC C1083 otherwise).
    target_link_libraries(bcos-framework PUBLIC evmone::evmone)
endif()
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
