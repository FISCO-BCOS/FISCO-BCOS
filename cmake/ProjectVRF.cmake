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
    if("${ARCHITECTURE}" MATCHES "aarch64")
        set(VRF_LIB_NAME libffi_vrf_aarch64.a)
    else()
        set(VRF_LIB_NAME libffi_vrf_generic64.a)
    endif()
elseif(APPLE)
    set(VRF_LIB_NAME libffi_vrf_mac.a)
else()
    message(FATAL "unsupported platform")
endif()


set(LIBVRF_SRC_FILE_URL  file://${THIRD_PARTY_ROOT}/libvrf-rust1.47.tar.gz)
set(LIBVRF_FILE_DIGEST SHA256=9a626bda04824f85575e28c72081f14ed5f1531e6247fff2edfb03cbef858570)

ExternalProject_Add(libvrf
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME libvrf.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    # URL https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/libvrf-rust1.47.tar.gz
        # https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/libvrf-rust1.47.tar.gz
    # URL_HASH SHA256=9a626bda04824f85575e28c72081f14ed5f1531e6247fff2edfb03cbef858570
    URL ${LIBVRF_SRC_FILE_URL}
    URL_HASH ${LIBVRF_FILE_DIGEST}
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    LOG_MERGED_STDOUTERR 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "/bin/cp ${CMAKE_SOURCE_DIR}/deps/src/libvrf/${VRF_LIB_NAME} ${CMAKE_SOURCE_DIR}/deps/lib/libffi_vrf.a"
)

ExternalProject_Get_Property(libvrf SOURCE_DIR)
add_library(VRF STATIC IMPORTED)

set(VRF_INCLUDE_DIR ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${VRF_INCLUDE_DIR})  # Must exist.

set(VRF_LIB "${CMAKE_SOURCE_DIR}/deps/lib/libffi_vrf.a")

set_property(TARGET VRF PROPERTY IMPORTED_LOCATION ${VRF_LIB})
set_property(TARGET VRF PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${VRF_INCLUDE_DIR})
add_dependencies(VRF libvrf)
unset(SOURCE_DIR)