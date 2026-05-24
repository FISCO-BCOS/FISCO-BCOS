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
# Function: Define coverage related functions using gcovr (cross-platform)
# Dependency: gcovr (pip install gcovr)
# ------------------------------------------------------------------------------
# Usage:
#   config_coverage(<target_name> [exclude_regex...])
#
#   <target_name>   - Name of the custom target (e.g., "coverage")
#   [exclude_regex]  - Optional regex patterns to exclude from coverage.
#                      Each is a regex matching source file paths.
#                      Example: "/usr/.*" ".*/test/.*" ".*/mock/.*"
#
#   Generates under ${CMAKE_BINARY_DIR}/CodeCoverage/:
#     - index.html          (HTML detailed report)
#     - coverage.xml        (Cobertura XML for CI tools)
#     - coverage.json       (JSON summary)
# ------------------------------------------------------------------------------
function(config_coverage TARGET)

    # ---- Parse optional exclude patterns from ARGN ----
    set(GCOVR_EXCLUDE_ARGS)
    foreach(PAT ${ARGN})
        list(APPEND GCOVR_EXCLUDE_ARGS --exclude "${PAT}")
    endforeach()

    # ---- Locate gcovr ----
    find_program(GCOVR_TOOL gcovr)
    message(STATUS "gcovr tool: ${GCOVR_TOOL}")

    if(NOT GCOVR_TOOL)
        message(FATAL_ERROR
            "Cannot find gcovr.\n"
            "  pip install gcovr\n"
            "  or: pip3 install gcovr\n"
            "  or: conda install -c conda-forge gcovr")
    endif()

    message(STATUS "coverage build dir: ${CMAKE_BINARY_DIR}")
    message(STATUS "coverage source dir: ${CMAKE_SOURCE_DIR}")
    message(STATUS "coverage target: ${TARGET}")

    # ---- Build the coverage report target ----
    add_custom_target(${TARGET}
        # Always ensure output directory exists (cross-platform)
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/CodeCoverage
        # Generate HTML + XML + JSON + text summary in one gcovr run
        COMMAND ${GCOVR_TOOL}
            --root ${CMAKE_SOURCE_DIR}
            --object-directory ${CMAKE_BINARY_DIR}
            --exclude "usr/.*"
            --exclude ".*vcpkg_installed.*"
            --exclude ".*boost.*"
            --exclude ".*test.*"
            --exclude ".*build.*"
            --exclude ".*deps.*"
            --exclude ".*/MacOS/.*"
            ${GCOVR_EXCLUDE_ARGS}
            --exclude-unreachable-branches
            --exclude-throw-branches
            --html-details ${CMAKE_BINARY_DIR}/CodeCoverage/index.html
            --html-title "FISCO BCOS Coverage Report"
            --xml ${CMAKE_BINARY_DIR}/CodeCoverage/coverage.xml
            --json ${CMAKE_BINARY_DIR}/CodeCoverage/coverage.json
            --print-summary
        COMMENT "Generating coverage report with gcovr..."
        VERBATIM
    )

    message(STATUS
        "Coverage target '${TARGET}' configured.\n"
        "  Build then run: cmake --build ${CMAKE_BINARY_DIR} --target ${TARGET}\n"
        "  Open: ${CMAKE_BINARY_DIR}/CodeCoverage/index.html")
endfunction()
