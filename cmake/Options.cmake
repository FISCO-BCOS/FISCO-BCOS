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

set(MARCH_TYPE "-march=native -mtune=generic")
if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    message(FATAL "The ${PROJECT_NAME} does not support compiling on 32-bit systems")
endif()

macro(configure_project)
     set(NAME ${PROJECT_NAME})

    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif()

    default_option(BUILD_STATIC OFF)

    default_option(NATIVE OFF)
    if(NATIVE)
        set(MARCH_TYPE "-march=native -mtune=native")
    endif()

    default_option(SANITIZE OFF)
    default_option(IPO OFF)
    if(IPO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    default_option(TESTS OFF)
    default_option(COVERAGE OFF)

    default_option(DEBUG OFF)
    if (DEBUG)
        add_definitions(-DFISCO_DEBUG)
    endif()

    default_option(FULLNODE ON)
    default_option(WITH_LIGHTNODE OFF)
    default_option(WITH_TIKV OFF)
    default_option(WITH_ETCD ON)
    default_option(WITH_TARS_SERVICES OFF)
    default_option(WITH_SM2_OPTIMIZE ON)
    default_option(WITH_CPPSDK ON)

    if(FULLNODE)
        list(APPEND VCPKG_MANIFEST_FEATURES "fullnode")
    endif()
    if(WITH_LIGHTNODE)
        list(APPEND VCPKG_MANIFEST_FEATURES "lightnode")
        add_compile_definitions(WITH_LIGHTNODE)
    endif()
    if(WITH_ETCD)
        list(APPEND VCPKG_MANIFEST_FEATURES "etcd")
        add_compile_definitions(WITH_ETCD)
    endif()
    if(WITH_TIKV)
        add_compile_definitions(WITH_TIKV)
    endif()
    if(WITH_SM2_OPTIMIZE)
        add_compile_definitions(WITH_SM2_OPTIMIZE)
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
    message("-- CMake              Cmake version and location   ${CMAKE_VERSION} (${CMAKE_COMMAND})")
    message("-- Compiler           C++ compiler version         ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message("-- CMAKE_BUILD_TYPE   Build type                   ${CMAKE_BUILD_TYPE}")
    message("-- TARGET_PLATFORM    Target platform              ${CMAKE_SYSTEM_NAME} ${ARCHITECTURE}")
    message("-- BUILD_STATIC       Build static                 ${BUILD_STATIC}")
    message("-- COVERAGE           Build code coverage          ${COVERAGE}")
    message("-- TESTS              Build tests                  ${TESTS}")
    message("-- NATIVE             Build native binary          ${NATIVE}")
    message("-- IPO                Enable IPO optimization      ${IPO}")
    message("-- SANITIZE           Enable sanitize              ${SANITIZE}")
    message("-- DEBUG              Enable debug                 ${DEBUG}")
    message("-- TOOLCHAIN_FILE     CMake toolchain file         ${CMAKE_TOOLCHAIN_FILE}")
    message("------------------------------------------------------------------------")
    message("-- Components")
    message("------------------------------------------------------------------------")
    message("-- FULLNODE           Build full node              ${FULLNODE}")
    message("-- WITH_LIGHTNODE     Enable light node            ${WITH_LIGHTNODE}")
    message("-- WITH_TIKV          Enable tikv                  ${WITH_TIKV}")
    message("-- WITH_ETCD          Enable etcd                  ${WITH_ETCD}")
    message("-- WITH_TARS_SERVICES Enable tars services         ${WITH_TARS_SERVICES}")
    message("-- WITH_SM2_OPTIMIZE  Enable sm2 optimize          ${WITH_SM2_OPTIMIZE}")
    message("-- WITH_CPPSDK        Enable cpp sdk               ${WITH_CPPSDK}")
    message("------------------------------------------------------------------------")
    message("")
endmacro()