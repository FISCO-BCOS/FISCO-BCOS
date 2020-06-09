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

set(VRF_SHA256 02f1e26203c5f6406abbecc4141834e77eb7063e6c93f9318ca08f944c57ea5e)
set(VRF_LIB_SUFFIX a)

ExternalProject_Add(vrf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME vrf.tgz
    DOWNLOAD_NO_PROGRESS 1
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    URL https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/vrf.tgz
        https://www.fisco.com.cn/cdn/deps/libs/vrf.tgz
    URL_HASH SHA256=${VRF_SHA256}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "/bin/cp ${CMAKE_SOURCE_DIR}/deps/src/vrf/*${VRF_LIB_SUFFIX} ${CMAKE_SOURCE_DIR}/deps/lib/"
)

ExternalProject_Get_Property(vrf SOURCE_DIR)
add_library(VRF STATIC IMPORTED)

set(VRF_INCLUDE_DIR ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${VRF_INCLUDE_DIR})  # Must exist.

if (APPLE)
    set(VRF_LIB "${CMAKE_SOURCE_DIR}/deps/lib/libffi_vrf_mac.${VRF_LIB_SUFFIX}")
else()
    set(VRF_LIB "${CMAKE_SOURCE_DIR}/deps/lib/libffi_vrf.${VRF_LIB_SUFFIX}")
endif()

set_property(TARGET VRF PROPERTY IMPORTED_LOCATION ${VRF_LIB})
set_property(TARGET VRF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VRF_INCLUDE_DIR})
add_dependencies(VRF vrf)
unset(SOURCE_DIR)