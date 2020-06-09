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

cmake_minimum_required(VERSION 3.7)
set(FISCO_BCOS_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake" CACHE PATH "The path to the cmake directory")
list(APPEND CMAKE_MODULE_PATH ${FISCO_BCOS_CMAKE_DIR})
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")

project(FISCO-BCOS VERSION "2.5.0")
# Suffix like "-rc1" e.t.c. to append to versions wherever needed.
set(VERSION_SUFFIX "")

find_package(Git QUIET)


set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)

# basic setting
include(Options)
include(CompilerSettings)
include(Dependencies)
configure_project()

# install dependencies
include(ProjectTBB)
include(ProjectSnappy)
include(ProjectLevelDB)
include(ProjectRocksDB)
include(ProjectMHD)
include(ProjectCryptopp)
include(ProjectSecp256k1)
include(ProjectJsonCpp)
include(ProjectJsonRpcCpp)
include(ProjectTASSL)
include(ProjectBoost)
include(ProjectLibzdb)
include(ProjectTCMalloc)
include(ProjectLibFF)
include(ProjectPaillier)
include(ProjectGroupSig)
include(ProjectEVMC)
include(ProjectVRF)

include_directories("${CMAKE_SOURCE_DIR}")

add_subdirectory(libchannelserver)
add_subdirectory(libdevcore)
add_subdirectory(libdevcrypto)
add_subdirectory(libethcore)
add_subdirectory(libinterpreter)
add_subdirectory(libstat)
add_subdirectory(libflowlimit)
add_subdirectory(libtxpool)
add_subdirectory(libstorage)
add_subdirectory(libprecompiled)
add_subdirectory(libnetwork)
add_subdirectory(libp2p)
add_subdirectory(libexecutive)
add_subdirectory(libmptstate)
add_subdirectory(libblockverifier)
add_subdirectory(libstoragestate)
add_subdirectory(libblockchain)
add_subdirectory(libsync)
add_subdirectory(libconsensus)
add_subdirectory(libledger)
add_subdirectory(librpc)
add_subdirectory(libinitializer)
add_subdirectory(libsecurity)
add_subdirectory(libeventfilter)
# generate executable binary fisco-bcos
add_subdirectory(fisco-bcos)


if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
# Requirements: doxygen graphviz
  set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/.Doxyfile.in)
  set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
  configure_file(${doxyfile_in} ${doxyfile} @ONLY)
# Add doc target
add_custom_target(doc COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                       COMMENT "Generating documentation with Doxygen..." VERBATIM)
elseif()
  message(WARNING "Doxygen is needed to build the documentation. Please install doxygen and graphviz")
endif()
