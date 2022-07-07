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

# common settings
set(MARCH_TYPE "-mtune=generic -fvisibility=hidden -fvisibility-inlines-hidden")
if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    message(FATAL "The ${PROJECT_NAME} does not support compiling on 32-bit systems")
endif()

EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
# for boost-ssl enable/disable native
set(ARCH_NATIVE OFF)
if ("${ARCHITECTURE}" MATCHES "aarch64" OR "${ARCHITECTURE}" MATCHES "arm64")
    set(ARCH_NATIVE ON)
endif()

macro(configure_project)
     set(NAME ${PROJECT_NAME})

    # Default to RelWithDebInfo configuration if no configuration is explicitly specified.
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif()

    default_option(BUILD_STATIC OFF)

    #ARCH TYPE
    default_option(NATIVE OFF)
    if ("${ARCHITECTURE}" MATCHES "aarch64" OR "${ARCHITECTURE}" MATCHES "arm64")
        default_option(NATIVE ON)
    endif()
    if(NATIVE)
        set(MARCH_TYPE "-march=native -mtune=native -fvisibility=hidden -fvisibility-inlines-hidden")
    endif()
    #SANITIZE
    default_option(SANITIZE OFF)

    #LTO
    default_option(IPO OFF)
    if(IPO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    # unit tests
    default_option(TESTS OFF)
    # code coverage
    default_option(COVERAGE OFF)

    #debug
    default_option(DEBUG OFF)
    if (DEBUG)
        add_definitions(-DFISCO_DEBUG)
    endif()

    default_option(TIKV OFF)
    default_option(ETCD OFF)

    default_option(BOOSTSSL OFF)
    default_option(CRYPTO OFF)
    default_option(FRAMEWORK OFF)
    default_option(UTILITIES OFF)
    
    default_option(TARSSERVICES OFF)

    default_option(ALL_COMPONENTS ON)
    if(ALL_COMPONENTS)
        set(BOOSTSSL ON)
        set(CRYPTO ON)
        set(FRAMEWORK ON)
        set(UTILITIES ON)
    endif()

    # Suffix like "-rc1" e.t.c. to append to versions wherever needed.
    if (NOT DEFINED VERSION_SUFFIX)
        set(VERSION_SUFFIX "")
    endif()
    print_config(${NAME})
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
    message("-- TIKV               Enable tikv                  ${TIKV}")
    message("-- ETCD               Enable etcd                  ${ETCD}")
    message("-- TOOLCHAIN_FILE     CMake toolchain file         ${CMAKE_TOOLCHAIN_FILE}")
    message("------------------------------------------------------------------------")
    message("-- ALL_COMPONENTS     Enable all components        ${ALL_COMPONENTS}")
    message("-- BOOSTSSL           Enable boostssl              ${BOOSTSSL}")
    message("-- CRYPTO             Enable crypto                ${CRYPTO}")
    message("-- FRAMEWORK          Enable framework             ${FRAMEWORK}")
    message("-- UTILITIES          Enable utilities             ${UTILITIES}")
    message("------------------------------------------------------------------------")
    message("-- TARSSERVICES       Enable tars services         ${TARSSERVICES}")
    message("")
endmacro()