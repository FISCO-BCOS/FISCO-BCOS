#------------------------------------------------------------------------------
# define and implement create_build_info function
# this function is used to generate BuildInfo.h (called by the source code)
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
function(create_build_info)
    # Set build platform; to be written to BuildInfo.h
    set(ETH_BUILD_OS "${CMAKE_SYSTEM_NAME}")

    if (CMAKE_COMPILER_IS_MINGW)
        set(ETH_BUILD_COMPILER "mingw")
    elseif (CMAKE_COMPILER_IS_MSYS)
        set(ETH_BUILD_COMPILER "msys")
    elseif (CMAKE_COMPILER_IS_GNUCXX)
        set(ETH_BUILD_COMPILER "g++")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(ETH_BUILD_COMPILER "clang")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
        set(ETH_BUILD_COMPILER "appleclang")
    else ()
        set(ETH_BUILD_COMPILER "unknown")
    endif ()

    set(ETH_BUILD_PLATFORM "${ETH_BUILD_OS}/${ETH_BUILD_COMPILER}")


    if (CMAKE_BUILD_TYPE)
        set(_cmake_build_type ${CMAKE_BUILD_TYPE})
    else()
        set(_cmake_build_type "${CMAKE_CFG_INTDIR}")
    endif()

    # Generate header file containing useful build information
    add_custom_target(BuildInfo.h ALL
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -DETH_SOURCE_DIR="${PROJECT_SOURCE_DIR}" 
        -DETH_BUILDINFO_IN="${ETH_CMAKE_DIR}/templates/BuildInfo.h.in" 
        -DETH_DST_DIR="${PROJECT_BINARY_DIR}/include" 
        -DETH_CMAKE_DIR="${ETH_CMAKE_DIR}"
        -DFISCO_BCOS_BUILD_TYPE="${_cmake_build_type}"
        -DFISCO_BCOS_BUILD_OS="${ETH_BUILD_OS}"
        -DFISCO_BCOS_BUILD_COMPILER="${ETH_BUILD_COMPILER}"
        -DFISCO_BCOS_BUILD_PLATFORM="${ETH_BUILD_PLATFORM}"
        -DFISCO_BCOS_BUILD_NUMBER="${FISCO_BCOS_BUILD_NUMBER}"
        -DFISCO_BCOS_VERSION_SUFFIX="${VERSION_SUFFIX}"
        -DPROJECT_VERSION="${PROJECT_VERSION}"
        -P "${ETH_SCRIPTS_DIR}/buildinfo.cmake"
        )
    include_directories(BEFORE ${PROJECT_BINARY_DIR})
endfunction()
