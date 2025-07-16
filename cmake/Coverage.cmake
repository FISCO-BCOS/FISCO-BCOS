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
# File: Coverage.cmake
# Function: Define coverage related functions
# ------------------------------------------------------------------------------
# REMOVE_FILE_PATTERN eg.: '/usr*' '${CMAKE_SOURCE_DIR}/deps**' '${CMAKE_SOURCE_DIR}/evmc*' ‘${CMAKE_SOURCE_DIR}/fisco-bcos*’
function(config_coverage TARGET REMOVE_FILE_PATTERN)
    find_program(LCOV_TOOL lcov)
    message(STATUS "lcov tool: ${LCOV_TOOL}")
    if (LCOV_TOOL)
        message(STATUS "coverage dir: " ${CMAKE_BINARY_DIR})
        message(STATUS "coverage TARGET: " ${TARGET})
        message(STATUS "coverage REMOVE_FILE_PATTERN: " ${REMOVE_FILE_PATTERN})
        if (APPLE)
            add_custom_target(${TARGET}
                COMMAND ${LCOV_TOOL} -keep-going --ignore-errors inconsistent,unmapped,source --rc lcov_branch_coverage=1 -o ${CMAKE_BINARY_DIR}/coverage.info.in -c -d ${CMAKE_BINARY_DIR}/
                COMMAND ${LCOV_TOOL} -keep-going --ignore-errors inconsistent,unmapped,source --rc lcov_branch_coverage=1 -r ${CMAKE_BINARY_DIR}/coverage.info.in '*MacOS*' '/usr*' '.*vcpkg_installed*' '.*boost/*' '*test*' '*build*' '*deps*' ${REMOVE_FILE_PATTERN} -o ${CMAKE_BINARY_DIR}/coverage.info
                COMMAND genhtml --keep-going --ignore-errors inconsistent,unmapped,source --rc lcov_branch_coverage=1 -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info)
        else()
            add_custom_target(${TARGET}
                COMMAND ${LCOV_TOOL} --ignore-errors mismatch,negative,gcov --branch-coverage -o ${CMAKE_BINARY_DIR}/coverage.info.in -c -d ${CMAKE_BINARY_DIR}/
                COMMAND ${LCOV_TOOL} --ignore-errors mismatch,negative,gcov --branch-coverage -r ${CMAKE_BINARY_DIR}/coverage.info.in '/usr*' '*vcpkg_installed*' '*boost*' '*test*' '*build*' '*deps*' ${REMOVE_FILE_PATTERN} -o ${CMAKE_BINARY_DIR}/coverage.info
                COMMAND genhtml --branch-coverage -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info)
        endif()
    else ()
        message(FATAL_ERROR "Can't find lcov tool. Please install lcov")
    endif()
endfunction()