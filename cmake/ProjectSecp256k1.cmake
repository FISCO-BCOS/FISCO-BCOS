#------------------------------------------------------------------------------
# install secp256k1
# @ ${SECP256K1_LIBRARY}: library path of secp256k1;
# @ ${SECP256K1_INCLUDE_DIR}: include path of secp256k1.
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

set(SECKP256K1_SRC_FILE_URL  file://${THIRD_PARTY_ROOT}/secp256k1.tar.gz)
set(SECKP256K1_FILE_DIGEST SHA256=02f8f05c9e9d2badc91be8e229a07ad5e4984c1e77193d6b00e549df129e7c3a)


ExternalProject_Add(secp256k1
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME secp256k1-ac8ccf29.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    # URL https://github.com/FISCO-BCOS/secp256k1/archive/ac8ccf29b8c6b2b793bc734661ce43d1f952977a.tar.gz
    # URL_HASH SHA256=02f8f05c9e9d2badc91be8e229a07ad5e4984c1e77193d6b00e549df129e7c3a
    URL ${SECKP256K1_SRC_FILE_URL}
    URL_HASH ${SECKP256K1_FILE_DIGEST}
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_LIST_DIR}/secp256k1/CMakeLists.txt <SOURCE_DIR>
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               -DCMAKE_POSITION_INDEPENDENT_CODE=${BUILD_SHARED_LIBS}
               ${_only_release_configuration}
            #    -DCMAKE_C_FLAGS=-Wa,-march=generic64
            #    -DCMAKE_CXX_FLAGS=-Wa,-march=generic64
               -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
               -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    LOG_CONFIGURE 1
    # BUILD_COMMAND ""
    LOG_BUILD 1
    LOG_INSTALL 1
    LOG_MERGED_STDOUTERR 1
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libsecp256k1.a
)

# Create imported library
ExternalProject_Get_Property(secp256k1 INSTALL_DIR)
add_library(Secp256k1 STATIC IMPORTED)
set(SECP256K1_LIBRARY ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}secp256k1${CMAKE_STATIC_LIBRARY_SUFFIX})
set(SECP256K1_INCLUDE_DIR ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${SECP256K1_INCLUDE_DIR})  # Must exist.
set_property(TARGET Secp256k1 PROPERTY IMPORTED_LOCATION ${SECP256K1_LIBRARY})
set_property(TARGET Secp256k1 PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${SECP256K1_INCLUDE_DIR})
add_dependencies(Secp256k1 secp256k1)
unset(INSTALL_DIR)
