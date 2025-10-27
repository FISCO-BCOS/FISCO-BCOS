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
    # Tools: lcov/genhtml are required. Ubuntu 24.04 ships lcov 2.x which needs explicit branch rc.
    find_program(LCOV_TOOL lcov)
    find_program(GENHTML_TOOL genhtml)
    if (NOT LCOV_TOOL)
        message(FATAL_ERROR "Can't find lcov tool. Please install lcov")
    endif()
    if (NOT GENHTML_TOOL)
        message(FATAL_ERROR "Can't find genhtml tool. Please install lcov (provides genhtml)")
    endif()

    message(STATUS "lcov tool: ${LCOV_TOOL}")
    message(STATUS "genhtml tool: ${GENHTML_TOOL}")
    message(STATUS "coverage dir: ${CMAKE_BINARY_DIR}")
    message(STATUS "coverage TARGET: ${TARGET}")
    if (REMOVE_FILE_PATTERN)
        message(STATUS "coverage REMOVE_FILE_PATTERN: ${REMOVE_FILE_PATTERN}")
    endif()

    # Detect gcov/llvm-cov for Ubuntu 24.04 (GCC 14 / Clang 18) and pass via env GCOV
    set(GCOV_ENV "")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Try versioned gcov first (e.g. gcov-14), then fallback to gcov
        # CMake doesn't expose a *_VERSION_MAJOR variable for compilers; extract it from the version string.
        if (CMAKE_CXX_COMPILER_VERSION)
            string(REGEX REPLACE "^([0-9]+).*" "\\1" _CXX_VER_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
        endif()
        set(_gcov_candidates gcov-${_CXX_VER_MAJOR} gcov)
        find_program(_GCOV_PATH ${_gcov_candidates})
        if (_GCOV_PATH)
            set(GCOV_ENV "GCOV=${_GCOV_PATH}")
        endif()
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # lcov consumes gcov-compatible output; use `llvm-cov gcov`
        find_program(_LLVM_COV llvm-cov)
        if (_LLVM_COV)
            # Keep as a single arg so spaces are preserved: GCOV="/usr/bin/llvm-cov gcov"
            set(GCOV_ENV "GCOV=${_LLVM_COV} gcov")
        endif()
    endif()
    if (GCOV_ENV)
        message(STATUS "coverage gcov tool set via env: ${GCOV_ENV}")
    else()
        message(WARNING "No explicit gcov tool configured; lcov will try system default. On Ubuntu 24.04 you may need gcov-14 or llvm-cov installed.")
    endif()

    # Common exclude patterns. Quote patterns to avoid shell globbing when using Makefile generator.
    set(_coverage_excludes
        "/usr/*"
        "${CMAKE_BINARY_DIR}/*"
        "*/CMakeFiles/*"
        "*/vcpkg_installed/*"
        "*/_deps/*"
        "*/thirdparty/*"
        "*/boost/*"
        "*/tests/*"
        "*/test/*"
        "*/build/*"
        "*/deps/*"
    )
    # Allow caller-provided extra remove patterns (can be a list)
    if (REMOVE_FILE_PATTERN)
        list(APPEND _coverage_excludes ${REMOVE_FILE_PATTERN})
    endif()

    # Assemble commands; always enable branch coverage rc and tolerant error handling across platforms.
    if (APPLE)
        add_custom_target(${TARGET}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/CodeCoverage
            COMMAND ${CMAKE_COMMAND} -E env ${GCOV_ENV} ${LCOV_TOOL} --keep-going --rc lcov_branch_coverage=1 -o ${CMAKE_BINARY_DIR}/coverage.info.in -c -d ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E env ${GCOV_ENV} ${LCOV_TOOL} --keep-going --rc lcov_branch_coverage=1 -r ${CMAKE_BINARY_DIR}/coverage.info.in "*MacOS*" 
                    ${_coverage_excludes} -o ${CMAKE_BINARY_DIR}/coverage.info
            COMMAND ${GENHTML_TOOL} --keep-going --ignore-errors inconsistent,unmapped,source --rc lcov_branch_coverage=1 -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info
        )
    else()
        add_custom_target(${TARGET}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/CodeCoverage
            COMMAND ${CMAKE_COMMAND} -E env ${GCOV_ENV} ${LCOV_TOOL} --keep-going --rc lcov_branch_coverage=1 -o ${CMAKE_BINARY_DIR}/coverage.info.in -c -d ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E env ${GCOV_ENV} ${LCOV_TOOL} --keep-going --rc lcov_branch_coverage=1 -r ${CMAKE_BINARY_DIR}/coverage.info.in 
                    ${_coverage_excludes} -o ${CMAKE_BINARY_DIR}/coverage.info
            COMMAND ${GENHTML_TOOL} --keep-going --rc lcov_branch_coverage=1 -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info
        )
    endif()
endfunction()