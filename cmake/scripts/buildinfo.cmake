#------------------------------------------------------------------------------
# generates BuildInfo.h
# 
# this module expects
# ETH_SOURCE_DIR - main CMAKE_SOURCE_DIR
# ETH_DST_DIR - main CMAKE_BINARY_DIR
# ETH_BUILD_TYPE
# ETH_BUILD_PLATFORM
# ETH_BUILD_NUMBER
# ETH_VERSION_SUFFIX
# ------------------------------------------------------------------------------
# This file is part of FISCO-BCOS.
#
# FISCO-BCOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# FISCO-BCOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------

macro(replace_if_different SOURCE DST)
    set(extra_macro_args ${ARGN})
    set(options CREATE)
    set(one_value_args)
    set(multi_value_args)
    cmake_parse_arguments(REPLACE_IF_DIFFERENT "${options}" "${one_value_args}" "${multi_value_args}" "${extra_macro_args}")

    if (REPLACE_IF_DIFFERENT_CREATE AND (NOT (EXISTS "${DST}")))
        file(WRITE "${DST}" "")
    endif()

    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${SOURCE}" "${DST}" RESULT_VARIABLE DIFFERENT OUTPUT_QUIET ERROR_QUIET)

    if (DIFFERENT)
        execute_process(COMMAND ${CMAKE_COMMAND} -E rename "${SOURCE}" "${DST}")
    else()
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove "${SOURCE}")
    endif()
endmacro()

if (NOT FISCO_BCOS_BUILD_TYPE)
    set(FISCO_BCOS_BUILD_TYPE "unknown")
endif()

if (NOT FISCO_BCOS_BUILD_PLATFORM)
    set(FISCO_BCOS_BUILD_PLATFORM "unknown")
endif()

execute_process(
    COMMAND git --git-dir=${ETH_SOURCE_DIR}/.git --work-tree=${ETH_SOURCE_DIR} rev-parse HEAD
    OUTPUT_VARIABLE FISCO_BCOS_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
) 

if (NOT FISCO_BCOS_COMMIT_HASH)
    set(FISCO_BCOS_COMMIT_HASH 0)
endif()

execute_process(
    COMMAND git --git-dir=${ETH_SOURCE_DIR}/.git --work-tree=${ETH_SOURCE_DIR} diff HEAD --shortstat
    OUTPUT_VARIABLE FISCO_BCOS_LOCAL_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)

execute_process(
    COMMAND date "+%Y%m%d %H:%M:%S" 
    OUTPUT_VARIABLE FISCO_BCOS_BUILD_TIME OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )

execute_process(
    COMMAND date "+%Y%m%d" 
    OUTPUT_VARIABLE FISCO_BCOS_BUILD_NUMBER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD 
    OUTPUT_VARIABLE FISCO_BCOS_BUILD_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )

if (FISCO_BCOS_LOCAL_CHANGES)
    set(FISCO_BCOS_CLEAN_REPO 0)
else()
    set(FISCO_BCOS_CLEAN_REPO 1)
endif()

set(TMPFILE "${ETH_DST_DIR}/BuildInfo.h.tmp")
set(OUTFILE "${ETH_DST_DIR}/BuildInfo.h")

configure_file("${ETH_BUILDINFO_IN}" "${TMPFILE}")

replace_if_different("${TMPFILE}" "${OUTFILE}" CREATE)

