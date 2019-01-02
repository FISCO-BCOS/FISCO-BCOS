#------------------------------------------------------------------------------
# Set compile options for FISCO-BCOS.
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
# change-list
# 2018/09/05: yujiechen
# 1. add DEBUG flag
# 2. add ETH_DEBUG definition when DEBUG flag has been set

macro(eth_default_option O DEF)
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

macro(configure_project)
     set(NAME ${PROJECT_NAME})

    # Default to RelWithDebInfo configuration if no configuration is explicitly specified.
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif()

    eth_default_option(BUILD_SHARED_LIBS OFF)
   
    eth_default_option(STATIC_BUILD OFF)
    #ARCH TYPE
    eth_default_option(ARCH_TYPE OFF)
    # unit tests
    eth_default_option(TESTS OFF)
    # code coverage
    eth_default_option(COVERAGE OFF)
    # log control
    eth_default_option(EASYLOG OFF)
    if (EASYLOG)
        add_definitions(-DFISCO_EASYLOG)
    endif()

    #debug
    eth_default_option(DEBUG OFF)
    if (DEBUG)
        add_definitions(-DETH_DEBUG)
    endif()


    # Define a matching property name of each of the "features".
    foreach(FEATURE ${ARGN})
        set(SUPPORT_${FEATURE} TRUE)
    endforeach()

    # CI Builds should provide (for user builds this is totally optional)
    # -DBUILD_NUMBER - A number to identify the current build with. Becomes TWEAK component of project version.
    # -DVERSION_SUFFIX - A string to append to the end of the version string where applicable.
    if (NOT DEFINED BUILD_NUMBER)
        # default is big so that local build is always considered greater
        # and can easily replace CI build for for all platforms if needed.
        # Windows max version component number is 65535
        set(BUILD_NUMBER 65535)
    endif()

    # Suffix like "-rc1" e.t.c. to append to versions wherever needed.
    if (NOT DEFINED VERSION_SUFFIX)
        set(VERSION_SUFFIX "")
    endif()

    include(EthBuildInfo)
    create_build_info()
    print_config(${NAME})
endmacro()

macro(print_config NAME)
    message("")
    message("------------------------------------------------------------------------")
    message("-- Configuring ${NAME}")
    message("------------------------------------------------------------------------")
    message("-- CMake ${CMAKE_VERSION} (${CMAKE_COMMAND})")
    message("-- CMAKE_BUILD_TYPE Build type                               ${CMAKE_BUILD_TYPE}")
    message("-- TARGET_PLATFORM  Target platform                          ${CMAKE_SYSTEM_NAME}")
    message("-- STATIC_BUILD     Build static                             ${STATIC_BUILD}")
    message("-- ARCH_TYPE                                                 ${ARCH_TYPE}")
if (SUPPORT_TESTS)
    message("-- TESTS            Build tests                              ${TESTS}")
endif()
    message("-- EasyLog          Enable easyLog                           ${EASYLOG}")
    message("------------------------------------------------------------------------")
    message("")
endmacro()

