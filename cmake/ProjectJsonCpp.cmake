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
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------
include(ExternalProject)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    set(JSONCPP_CMAKE_COMMAND emcmake cmake)
else()
    set(JSONCPP_CMAKE_COMMAND ${CMAKE_COMMAND})
endif()

ExternalProject_Add(jsoncpp
    PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps
    DOWNLOAD_NAME jsoncpp-1.7.7.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/open-source-parsers/jsoncpp/archive/1.7.7.tar.gz
        https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/jsoncpp-1.7.7.tar.gz
        https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/jsoncpp-1.7.7.tar.gz
    URL_HASH SHA256=087640ebcf7fbcfe8e2717a0b9528fff89c52fcf69fa2a18cc2b538008098f97
    CMAKE_COMMAND ${JSONCPP_CMAKE_COMMAND}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               # Build static lib but suitable to be included in a shared lib.
               -DCMAKE_POSITION_INDEPENDENT_CODE=${BUILD_SHARED_LIBS}
               ${_only_release_configuration}
               -DJSONCPP_WITH_TESTS=OFF
               -DJSONCPP_WITH_PKGCONFIG_SUPPORT=OFF
               	# -DCMAKE_C_FLAGS=-Wa,-march=generic64
               	# -DCMAKE_CXX_FLAGS=-Wa,-march=generic64
               	-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        		-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    BUILD_COMMAND ""
    ${_overwrite_install_command}
    LOG_INSTALL 1
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libjsoncpp.a
)

# Create jsoncpp imported library
ExternalProject_Get_Property(jsoncpp INSTALL_DIR)
add_library(JsonCpp STATIC IMPORTED GLOBAL)
set(JSONCPP_LIBRARY ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jsoncpp${CMAKE_STATIC_LIBRARY_SUFFIX})
set(JSONCPP_INCLUDE_DIR ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${JSONCPP_INCLUDE_DIR})  # Must exist.
set_property(TARGET JsonCpp PROPERTY IMPORTED_LOCATION ${JSONCPP_LIBRARY})
set_property(TARGET JsonCpp PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${JSONCPP_INCLUDE_DIR})
add_dependencies(JsonCpp jsoncpp)
unset(INSTALL_DIR)
