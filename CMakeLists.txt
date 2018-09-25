#------------------------------------------------------------------------------
# Top-level CMake file for FISCO-BCOS.
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

cmake_minimum_required(VERSION 3.4)
set(FISCO_BCOS_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake" CACHE PATH "The path to the cmake directory")
list(APPEND CMAKE_MODULE_PATH ${FISCO_BCOS_CMAKE_DIR})

project(FISCO-BCOS VERSION "2.0")

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/evmc/.git)
    message(FATAL_ERROR "Git submodules not initialized, execute:\n  git submodule update --init")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY On)

# basic setting
include(EthOptions)
include(EthCompilerSettings)
include(EthDependencies)

# install dependencies
include(ProjectCryptopp)
include(ProjectSecp256k1)
include(ProjectJsonCpp)
include(ProjectJsonRpcCpp)
include(ProjectBoost)

#Global include path for all libs.
include_directories("${CMAKE_SOURCE_DIR}" "utils")

configure_project(TESTS)
# generate executable binary fisco-bcos
add_subdirectory(fisco-bcos)
add_subdirectory(fisco-bcos/p2p)
add_subdirectory(libdevcore)
add_subdirectory(libdevcrypto)
add_subdirectory(libethcore)
add_subdirectory(evmc)
add_subdirectory(libinterpreter)
add_subdirectory(libevm)
add_subdirectory(libp2p)
add_subdirectory(libmptstate)
add_subdirectory(libexecutivecontext)
add_subdirectory(libblockverifier)
add_subdirectory(libstorage)

add_subdirectory(libtxpool)
add_subdirectory(libblocksync)
add_subdirectory(libconsensus)
add_subdirectory(libblockmanager)

if (TESTS)
        enable_testing()
        add_subdirectory(test)
endif()
