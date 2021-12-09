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

cmake_minimum_required(VERSION 3.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
    # set(URL_BASE "github.com.cnpmjs.org")
endif()
include(FetchContent)

# fetch bcos-cmake-scripts
set(BCOS_CMAKE_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts")
FetchContent_Declare(bcos-cmake-scripts
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-cmake-scripts.git
    GIT_TAG 9b2ac5b979cbc97f640b2285feb2c74a18cfb935
    SOURCE_DIR ${BCOS_CMAKE_SCRIPTS_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

set(HUNTER_PACKAGE_LOG_BUILD ON CACHE STRING "Hunter output build info to log")
set(HUNTER_PACKAGE_LOG_INSTALL ON CACHE STRING "Hunter output install info to log")
set(HUNTER_STATUS_DEBUG OFF CACHE STRING "Hunter debug output")
set(HUNTER_USE_CACHE_SERVERS NO CACHE STRING "Download cache from cache server" FORCE)
# set(HUNTER_STATUS_PRINT OFF CACHE STRING "Hunter status output")
# set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")

# fetch bcos-proto
set(BCOS_PROTO_NAME bcos-proto)
set(BCOS_PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libprotocol/${BCOS_PROTO_NAME})
FetchContent_Declare(${BCOS_PROTO_NAME}
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-proto.git
    GIT_TAG 28e5620d348bd82dff649ae37ca25120d5a6223e
    SOURCE_DIR ${BCOS_PROTO_PATH}
    CMAKE_ARGS -DHUNTER_PACKAGE_LOG_BUILD=${HUNTER_PACKAGE_LOG_BUILD}
               -DHUNTER_PACKAGE_LOG_INSTALL=${HUNTER_PACKAGE_LOG_INSTALL}
               -DHUNTER_STATUS_DEBUG=${HUNTER_STATUS_DEBUG}
)
FetchContent_MakeAvailable(bcos-cmake-scripts ${BCOS_PROTO_NAME})

# cmake settings
set(BCOS_FRAMEWORK_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE PATH "The cmake path for the library")
list(APPEND CMAKE_MODULE_PATH ${BCOS_CMAKE_SCRIPTS_DIR})
list(APPEND CMAKE_MODULE_PATH ${BCOS_FRAMEWORK_CMAKE_DIR})

# init hunter
include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/203039fdd0826330facd9c3841082dbf35d0c808.tar.gz"
    SHA1 "26ca1840eb26da410913469f2f278569c8d383ae"
    FILEPATH "${BCOS_FRAMEWORK_CMAKE_DIR}/config.cmake"
)
# set CMP0074 to NEW to suppress warnning, https://cmake.org/cmake/help/latest/policy/CMP0074.html
cmake_policy(VERSION 3.12)
cmake_policy(SET CMP0074 NEW)

project(bcos-framework VERSION "3.0.0")
# Suffix like "-rc1" e.t.c. to append to versions wherever needed.
set(VERSION_SUFFIX "-rc1")

# basic setting
include(Options)
configure_project()
include(CompilerSettings)

# install dependencies
# install boost dependency
hunter_add_package(Boost COMPONENTS all)
# install protobuf dependency
hunter_add_package(Protobuf)
# install gsl dependency
hunter_add_package(Microsoft.GSL)
find_package(Microsoft.GSL CONFIG REQUIRED)
# install tbb dependency
hunter_add_package(tbb)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_INSTALL_INCLUDEDIR})
include_directories(${CMAKE_INSTALL_INCLUDEDIR}/include)

# set library target name
set(UTILITIES_TARGET "utilities")
set(STORAGE_TARGET "storage")
set(CODEC_TARGET "codec")
set(PROTOCOL_TARGET "protocol")
set(PBPROTOCOL_TARGET "protocol-pb")
set(SYNC_TARGET "sync")
set(SEALER_TARGET "sealer")
set(TOOL_TARGET "tool")

# define the generated proto file path
set(PROTO_GENERATE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libprotocol/${BCOS_PROTO_NAME})
set(PROTO_PATH ${PROJECT_SOURCE_DIR})

# generate sync protobuf files
set(SYNC_PROTO_GENERATE_BASE_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(SYNC_PROTO_SUB_DIR "libsync/protocol/proto")
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})

# add subdirectories to build the libraries
add_subdirectory(libutilities)
add_subdirectory(libcodec)
add_subdirectory(libstorage)
add_subdirectory(libprotocol)
add_subdirectory(libprotocol/protobuf)
add_subdirectory(libsync)
add_subdirectory(libsealer)
add_subdirectory(libtool)

# test releated
if (TESTS)
    hunter_add_package(jsoncpp)
    find_package(jsoncpp CONFIG REQUIRED)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE True)
    add_subdirectory(test)
endif()

include(InstallConfig)
install(
    TARGETS ${UTILITIES_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${CODEC_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${PROTOCOL_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${PBPROTOCOL_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${STORAGE_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${SYNC_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${SEALER_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    TARGETS ${TOOL_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

include(InstallInclude)
# for doxygen
include(BuildDocs)
buildDoc(bcos-framework-doc)

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("framework-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
