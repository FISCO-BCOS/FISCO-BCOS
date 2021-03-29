#------------------------------------------------------------------------------
# HTTP client from JSON RPC CPP requires curl library. It can find it itself,
# but we need to know the libcurl location for static linking.
#
# HTTP server from JSON RPC CPP requires microhttpd library. It can find it itself,
# but we need to know the MHD location for static linking.
# ------------------------------------------------------------------------------
# This file is part of TIANHE-CHAIN.
#
# TIANHE-CHAIN is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TIANHE-CHAIN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TIANHE-CHAIN.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 tianhe-dev contributors.
#------------------------------------------------------------------------------
#find_package(CURL REQUIRED)
# find_package(MHD REQUIRED)
set(CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               -DCMAKE_BUILD_TYPE=Release
               # Build static lib but suitable to be included in a shared lib.
               -DCMAKE_POSITION_INDEPENDENT_CODE=${BUILD_SHARED_LIBS}
               -DBUILD_STATIC_LIBS=ON
               -DBUILD_SHARED_LIBS=OFF
               -DUNIX_DOMAIN_SOCKET_SERVER=OFF
               -DUNIX_DOMAIN_SOCKET_CLIENT=OFF
               -DHTTP_SERVER=ON
               -DREDIS_SERVER=OFF
               -DREDIS_CLIENT=OFF
               -DFILE_DESCRIPTOR_SERVER=OFF
               -DFILE_DESCRIPTOR_CLIENT=OFF
               -DTCP_SOCKET_SERVER=OFF
               -DTCP_SOCKET_CLIENT=OFF
               -fprofile-arcs
	       # add client by alvin
	           -DHTTP_CLIENT=ON
               # DTCP_SOCKET_CLIENT
               -DCOMPILE_TESTS=NO
               -DCOMPILE_STUBGEN=NO
               -DCOMPILE_EXAMPLES=NO
               # Point to jsoncpp library.
               -DJSONCPP_INCLUDE_DIR=${JSONCPP_INCLUDE_DIR}
               # Select jsoncpp include prefix: <json/...> or <jsoncpp/json/...>
               -DJSONCPP_INCLUDE_PREFIX=json
               -DJSONCPP_LIBRARY=${JSONCPP_LIBRARY}
               #-DCURL_INCLUDE_DIR=${CURL_INCLUDE_DIR}
               #-DCURL_LIBRARY=${CURL_LIBRARY}
               -DMHD_INCLUDE_DIR=${MHD_INCLUDE_DIR}
               -DMHD_LIBRARY=${MHD_LIBRARY}
               -DCMAKE_C_FLAGS=-fvisibility=hidden -fvisibility-inlines-hidden
               -DCMAKE_CXX_FLAGS=-fvisibility=hidden -fvisibility-inlines-hidden
               -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
               -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
               )



ExternalProject_Add(jsonrpccpp
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME jsonrpccpp-0.7.0.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    #URL https://codeload.github.com/cinemast/libjson-rpc-cpp/tar.gz/v1.0.0 
    URL http://code.tianhecloud.com:33012/TH_ALLIES/libjson-rpc-cpp/archive/0.7.0.tar.gz
    #URL https://github.com/cinemast/libjson-rpc-cpp/archive/v0.7.0.tar.gz
    #URL_HASH SHA256=669c2259909f11a8c196923a910f9a16a8225ecc14e6c30e2bcb712bab9097eb
    URL_HASH SHA256=8b519b8389ab83149d424369aef89e572649d35484bbe58d3253a94bac03b3ef
    # On Windows it tries to install this dir. Create it to prevent failure.
    PATCH_COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/win32-deps/include
    CMAKE_ARGS ${CMAKE_ARGS}
    LOG_CONFIGURE 1
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libjsonrpccpp-client.a  ${CMAKE_SOURCE_DIR}/deps/lib/libjsonrpccpp-server.a ${CMAKE_SOURCE_DIR}/deps/lib/libjsonrpccpp-common.a
)

add_dependencies(jsonrpccpp jsoncpp)
#add_dependencies(jsonrpccpp curl)
add_dependencies(jsonrpccpp MHD)

ExternalProject_Get_Property(jsonrpccpp SOURCE_DIR)
ExternalProject_Get_Property(jsonrpccpp INSTALL_DIR)
set(JSONRPCCPP_INCLUDE_DIR ${SOURCE_DIR}/src)
file(MAKE_DIRECTORY ${JSONRPCCPP_INCLUDE_DIR})  # Must exist.

add_library(JsonRpcCpp::Common STATIC IMPORTED)
set_property(TARGET JsonRpcCpp::Common PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jsonrpccpp-common${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET JsonRpcCpp::Common PROPERTY INTERFACE_LINK_LIBRARIES JsonCpp)
set_property(TARGET JsonRpcCpp::Common PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${JSONRPCCPP_INCLUDE_DIR} ${JSONCPP_INCLUDE_DIR})
add_dependencies(JsonRpcCpp::Common jsonrpccpp)

add_library(JsonRpcCpp::Client STATIC IMPORTED)
set_property(TARGET JsonRpcCpp::Client PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jsonrpccpp-client${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET JsonRpcCpp::Client PROPERTY INTERFACE_LINK_LIBRARIES JsonRpcCpp::Common  )
#set_property(TARGET JsonRpcCpp::Client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CURL_INCLUDE_DIR})
add_dependencies(JsonRpcCpp::Client jsonrpccpp)

add_library(JsonRpcCpp::Server STATIC IMPORTED)
set_property(TARGET JsonRpcCpp::Server PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jsonrpccpp-server${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET JsonRpcCpp::Server PROPERTY INTERFACE_LINK_LIBRARIES JsonRpcCpp::Common MHD)
# set_property(TARGET JsonRpcCpp::Server PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MHD_INCLUDE_DIR})
add_dependencies(JsonRpcCpp::Server jsonrpccpp)

unset(BINARY_DIR)
unset(INSTALL_DIR)
unset(CMAKE_ARGS)
