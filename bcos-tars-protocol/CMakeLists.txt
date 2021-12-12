#------------------------------------------------------------------------------
# Top-level CMake file for bcos-tars-protocol
# ------------------------------------------------------------------------------
# Copyright (C) 2021 bcos-tars-protocol
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
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

project(bcos-tars-protocol VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

hunter_add_package(tarscpp)
find_package(tarscpp CONFIG REQUIRED)

# for tars generator
set(TARS_HEADER_DIR ${CMAKE_BINARY_DIR}/bcos-tars-protocol/tars)

file(GLOB_RECURSE TARS_INPUT "*.tars")
# generate tars
if (TARS_INPUT)
    foreach(TARS_FILE ${TARS_INPUT})
        get_filename_component(TARS_NAME ${TARS_FILE} NAME_WE)
        get_filename_component(TARS_PATH ${TARS_FILE} PATH)
        add_custom_command(
            OUTPUT ${TARS_HEADER_DIR}/${TARS_NAME}.h
            WORKING_DIRECTORY ${TARS_PATH}
            COMMAND tarscpp::tars2cpp ${TARS_FILE} --dir=${TARS_HEADER_DIR}
            COMMENT "generating ${TARS_FILE} to ${TARS_HEADER_DIR}"
            VERBATIM
        )
        list(APPEND OUT_TARS_H_LIST ${TARS_HEADER_DIR}/${TARS_NAME}.h)
    endforeach()
endif()

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${OUT_TARS_H_LIST}")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_BINARY_DIR}/)
include_directories(${TARSCPP_ROOT}/include)

aux_source_directory(./protocol SRC_LIST)
aux_source_directory(./client SRC_LIST)

add_library(protocol-tars ${SRC_LIST} ${OUT_TARS_H_LIST})
target_compile_options(protocol-tars PRIVATE -Wno-error -Wno-unused-variable)
target_link_libraries(protocol-tars PUBLIC framework tarscpp::tarsutil tarscpp::tarsparse tarscpp::tarsservant)

if (TESTS)
    # fetch bcos-test
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

# # for doxygen
# include(BuildDocs)
# buildDoc(bcos-tars-protocol-doc)

# for code coverage
# if (COVERAGE)
#     include(Coverage)
#     config_coverage("tars-protocol-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
# endif ()
