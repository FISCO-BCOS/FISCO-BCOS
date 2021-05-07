#------------------------------------------------------------------------------
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
# (c) 2016-2020 fisco-dev contributors.
#------------------------------------------------------------------------------

include(ExternalProject)

if("${CMAKE_HOST_SYSTEM_NAME}" MATCHES "Linux")
    set(SDF_LIB_NAME libsdf-crypto_arm.a)
elseif(APPLE)
    message(FATAL "HSM  SDF only support Linux, the ${CMAKE_HOST_SYSTEM_NAME} ${ARCHITECTURE} is not supported.")
else()
    message(FATAL "HSM  SDF only support Linux, the ${CMAKE_HOST_SYSTEM_NAME} ${ARCHITECTURE} is not supported.")
endif()

ExternalProject_Add(libsdf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME sdf.zip
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/WeBankBlockchain/hsm-crypto/archive/refs/heads/v1.0.0.zip   
    URL_HASH SHA256=eee9f05add9b590f7f9c1ec86fee6e841b4bb08a94cc2ffeaebed306118940f7
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "/bin/bash ${CMAKE_SOURCE_DIR}/deps/src/libsdf/install.sh"
)

ExternalProject_Get_Property(libsdf SOURCE_DIR)
add_library(SDF STATIC IMPORTED)

set(HSM_INCLUDE_DIR ${SOURCE_DIR}/include)
set(SDF_INCLUDE_DIR ${SOURCE_DIR}/include/sdf)
file(MAKE_DIRECTORY ${SDF_INCLUDE_DIR})  # Must exist.

set(SDF_LIB "${SOURCE_DIR}/lib/libsdf-crypto_arm.a")

set_property(TARGET SDF PROPERTY IMPORTED_LOCATION ${SDF_LIB})
set_property(TARGET SDF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HSM_INCLUDE_DIR} ${SDF_INCLUDE_DIR})
add_dependencies(SDF libsdf)
unset(SOURCE_DIR)