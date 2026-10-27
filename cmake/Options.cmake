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
# File: Options.cmake
# Function: Define common compilation options
# ------------------------------------------------------------------------------

macro(default_option O DEF)
    if (DEFINED ${O})
        if (${${O}})
            set(${O} ON)
        else ()
            set(${O} OFF)
        endif()
    else ()
        set(${O} ${DEF})
    endif()
endmacro()

set(MARCH_TYPE "-mtune=generic")
if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    message(FATAL "The ${PROJECT_NAME} does not support compiling on 32-bit systems")
endif()

EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
macro(configure_project)
     set(NAME ${PROJECT_NAME})

    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif()

    default_option(BUILD_STATIC OFF)

    default_option(NATIVE OFF)
    if(NOT LINKER)
        set(LINKER "default")
    endif()
    if(NATIVE)
        set(MARCH_TYPE "-march=native -mtune=native")
    endif()
    default_option(SANITIZE_ADDRESS OFF)
    default_option(SANITIZE_THREAD OFF)
    # clang-tidy / clangd need a compilation database. Harmless for Ninja/Make.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    default_option(IPO OFF)
    if(IPO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    # Unity build is the repo-wide default; individual targets opt out with
    # set_target_properties(<target> PROPERTIES UNITY_BUILD OFF) and individual
    # sources with SKIP_UNITY_BUILD_INCLUSION. CompilerSettings.cmake applies
    # these as CMAKE_UNITY_BUILD / CMAKE_UNITY_BUILD_BATCH_SIZE.
    # Batch size stays at CMake's default 8: measured on GCC 16 / MinSizeRel / -j20
    # (20-core box, heavy-lib subset: executor, rpc, ledger, pbft, txpool, sync,
    # scheduler, ...):
    #   8 -> 96.4s wall / 2.8GB max TU; 16 -> 95.1s / 2.8GB; 32 -> 116s / 3.4GB;
    #   0 (one TU per target) -> 113s / 3.4GB.
    # 16 buys ~1.3% wall time on that subset, but the library subset is not where the memory
    # ceiling lives: the test targets peak higher (test-bcos-rpc / test-transaction-scheduler
    # reached ~3.7GB at batch 16; the batch-8 peak is unmeasured - follow-up issue). 8 matches
    # 16 on wall time at lower peak memory and half the incremental-rebuild granularity, so it
    # is the default; bump it locally with -DUNITY_BUILD_BATCH_SIZE=N if a full-build
    # measurement says otherwise.
    default_option(UNITY_BUILD ON)
    if(NOT DEFINED UNITY_BUILD_BATCH_SIZE)
        set(UNITY_BUILD_BATCH_SIZE "8")
    endif()

    default_option(TESTS OFF)
    default_option(COVERAGE OFF)

    default_option(DEBUG OFF)
    if (DEBUG)
        add_definitions(-DFISCO_DEBUG)
    endif()

    default_option(FULLNODE ON)
    default_option(WITH_LIGHTNODE OFF)
    default_option(WITH_TARS_SERVICES OFF)
    default_option(WITH_CPPSDK ON)
    default_option(WITH_SWIG_SDK OFF)
    default_option(WITH_BENCHMARK ON)
    default_option(WITH_VTUNE_ITT OFF)
    default_option(ONLY_CPP_SDK OFF)
    default_option(TOOLS OFF)
    # Build the OP-Stack L2 Solidity contracts (bcos-l2-contracts/) via forge.
    # Default OFF so the C++ main build does not depend on the forge toolchain.
    default_option(WITH_L2_CONTRACTS OFF)

    if((NOT FULLNODE) AND (NOT WITH_LIGHTNODE) AND WITH_CPPSDK)
        set(ONLY_CPP_SDK ON)
    endif()

    if(NOT WITH_VTUNE_ITT)
        add_definitions(-DINTEL_NO_ITTNOTIFY_API)
    else()
        add_definitions(-DTBB_USE_PROFILING_TOOLS=1)
    endif()
    if(FULLNODE)
        list(APPEND VCPKG_MANIFEST_FEATURES "fullnode")
    endif ()
    if (TESTS)
        list(APPEND VCPKG_MANIFEST_FEATURES "tests")
    endif ()
    if (ONLY_CPP_SDK)
        add_compile_definitions(ONLY_CPP_SDK)
        add_definitions(-DONLY_CPP_SDK)
        #list(APPEND VCPKG_MANIFEST_FEATURES "cppsdk")
    endif()
    if(WITH_LIGHTNODE)
        list(APPEND VCPKG_MANIFEST_FEATURES "lightnode")
        add_compile_definitions(WITH_LIGHTNODE)
    endif()
    if(WITH_TARS_SERVICES)
        add_compile_definitions(WITH_TARS_SERVICES)
    endif()

    if(NOT ALLOCATOR)
        set(ALLOCATOR "defalut")
    endif()

    if(ALLOCATOR STREQUAL "tcmalloc")
        add_compile_definitions(USE_TCMALLOC)
        list(APPEND VCPKG_MANIFEST_FEATURES "tcmalloc")
    elseif(ALLOCATOR STREQUAL "mimalloc")
        list(APPEND VCPKG_MANIFEST_FEATURES "mimalloc")
    else()
        set(ALLOCATOR "default")
    endif()
    if (NOT DEFINED VERSION_SUFFIX)
        set(VERSION_SUFFIX "")
    endif()
endmacro()

macro(print_config NAME)
    message("")
    message("------------------------------------------------------------------------")
    message("-- Configuring ${NAME} ${PROJECT_VERSION}${VERSION_SUFFIX}")
    message("------------------------------------------------------------------------")
    message("-- CMAKE              Cmake version and location   ${CMAKE_VERSION} (${CMAKE_COMMAND})")
    message("-- COMPILER           C++ compiler version         ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message("-- LINKER             C++ linker                   ${LINKER}")
    message("-- CMAKE_BUILD_TYPE   Build type                   ${CMAKE_BUILD_TYPE}")
    message("-- TARGET_PLATFORM    Target platform              ${CMAKE_SYSTEM_NAME} ${ARCHITECTURE}")
    message("-- BUILD_STATIC       Build static                 ${BUILD_STATIC}")
    message("-- COVERAGE           Build code coverage          ${COVERAGE}")
    message("-- TESTS              Build tests                  ${TESTS}")
    message("-- NATIVE             Build native binary          ${NATIVE}")
    message("-- IPO                Enable IPO optimization      ${IPO}")
    message("-- UNITY_BUILD        Enable unity build           ${UNITY_BUILD} (batch size ${UNITY_BUILD_BATCH_SIZE})")
    message("-- TOOLS              Tools                        ${TOOLS}")
    message("-- SANITIZE_ADDRESS   Enable sanitize              ${SANITIZE_ADDRESS}")
    message("-- SANITIZE_THREAD    Enable sanitize              ${SANITIZE_THREAD}")
    message("-- TOOLCHAIN_FILE     CMake toolchain file         ${CMAKE_TOOLCHAIN_FILE}")
    message("-- ALLOCATOR          Allocator                    ${ALLOCATOR}")
    message("------------------------------------------------------------------------")
    message("-- Components")
    message("------------------------------------------------------------------------")
    message("-- FULLNODE           Build full node              ${FULLNODE}")
    message("-- WITH_LIGHTNODE     Enable light node            ${WITH_LIGHTNODE}")
    message("-- WITH_TARS_SERVICES Enable tars services         ${WITH_TARS_SERVICES}")
    message("-- WITH_CPPSDK        Enable cpp sdk               ${WITH_CPPSDK}")
    message("-- WITH_SWIG_SDK      Enable swig sdk              ${WITH_SWIG_SDK}")
    message("-- WITH_VTUNE_ITT     Enable vtune itt api support ${WITH_VTUNE_ITT}")
    message("-- ONLY_CPP_SDK       Only build cpp sdk           ${ONLY_CPP_SDK}")
    message("-- WITH_L2_CONTRACTS  Build L2 solidity contracts  ${WITH_L2_CONTRACTS}")
    message("------------------------------------------------------------------------")
    message("")
endmacro()