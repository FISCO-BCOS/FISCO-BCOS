#------------------------------------------------------------------------------
# install json cpp from git
# @ ${JSONCPP_INCLUDE_DIR}: jsoncpp include path
# @ ${JSONCPP_LIBRARY}: jsoncpp libbrary path
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
# (c) 2016-2019 fisco-dev contributors.
#------------------------------------------------------------------------------
include(ExternalProject)

if (APPLE)
    set(WEDPR_URL https://github.com/WeDPR/Binary/releases/download/v0.1/mac_libffi_storage.tar.gz)
    set(WEDPR_SHA256 703c9c99fb1e1d43880466be968db9047d53ae861b41f34b3a07db4c4beccb06)
else()
    set(WEDPR_URL https://github.com/WeDPR/Binary/releases/download/v0.1/linux_libffi_storage.tar.gz)
    set(WEDPR_SHA256 62a7eef20c2a0b23a87b657af548ffb1f0b289b8ee56c477dd8bc99cdc6103a1)
endif()

ExternalProject_Add(WeDPR
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME libwedpr-0.1.0-lib.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    BUILD_IN_SOURCE 1
    URL ${WEDPR_URL}
    URL_HASH SHA256=${WEDPR_SHA256}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "cp ${CMAKE_SOURCE_DIR}/deps/src/WeDPR/* ${CMAKE_SOURCE_DIR}/deps/lib/"
)
