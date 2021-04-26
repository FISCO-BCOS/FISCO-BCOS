include(ExternalProject)
include(GNUInstallDirs)

set(PBC_CMAKE_PATH  ${CMAKE_SOURCE_DIR}/deps/src/GroupSigLib/cmake/ProjectPbc.cmake)
set(PBC_SIG_CMAKE_PATH  ${CMAKE_SOURCE_DIR}/deps/src/GroupSigLib/cmake/ProjectPbcSig.cmake)

if (APPLE AND ${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "arm64")
    set(PBC_LDFLAG_STR "set\(PBC_LDFLAG\ -L/opt/homebrew/lib\)")
    set(PBC_SIG_LDFLAG_STR "set\(PBC_SIG_LDFLAG\ \"-L\${CMAKE_SOURCE_DIR}/deps/lib -L/opt/homebrew/lib\"\)")

    set(PBC_PBCSTRAP_COMMAND_STR "set\(PBC_PBCSTRAP_COMMAND\ ./configure\ LDFLAGS=\${PBC_LDFLAG}\ --prefix=\${CMAKE_SOURCE_DIR}/deps\)")
    set(PBC_SIG_CONFIG_COMMAND_STR "set\(PBC_CONFIG_COMMAND\ ./configure\ CPPFLAGS=-I\${CMAKE_SOURCE_DIR}/deps/include LDFLAGS=\${PBC_SIG_LDFLAG}\ --prefix=\${CMAKE_SOURCE_DIR}/deps\)")
else()
    set(PBC_LDFLAG_STR "")
    set(PBC_SIG_LDFLAG_STR "")

    set(PBC_PBCSTRAP_COMMAND_STR "set\(PBC_PBCSTRAP_COMMAND\ ./configure\ --prefix=\${CMAKE_SOURCE_DIR}/deps\)")
    set(PBC_SIG_CONFIG_COMMAND_STR "set\(PBC_CONFIG_COMMAND\ ./configure\ CPPFLAGS=-I\${CMAKE_SOURCE_DIR}/deps/include LDFLAGS=-L\${CMAKE_SOURCE_DIR}/deps/lib\ --prefix=\${CMAKE_SOURCE_DIR}/deps\)")
endif()


ExternalProject_Add(GroupSigLib
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME group_sig_lib-868ec9ba.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/FISCO-BCOS/group-signature-lib/archive/868ec9bad9facc8bb6059216e913194c08a62cfb.tar.gz
    URL_HASH SHA256=a038561bd3f956e38fa4a49114a7386f47950c03d81ddb0ac9fb479889aa13f4
    PATCH_COMMAND  ${SED_CMMAND} -e "1s/^//p" CMakeLists.txt
    PATCH_COMMAND && ${SED_CMMAND} -e "1s#^.*#project(\"group_sig\")#" CMakeLists.txt
    PATCH_COMMAND && ${SED_CMMAND} -e "2s/^//p"  ${PBC_CMAKE_PATH}
    PATCH_COMMAND && ${SED_CMMAND} -e "2s#^.*#${PBC_LDFLAG_STR}#" ${PBC_CMAKE_PATH}
    PATCH_COMMAND && ${SED_CMMAND} -e "3s/^//p"  ${PBC_SIG_CMAKE_PATH}
    PATCH_COMMAND && ${SED_CMMAND} -e "3s#^.*#${PBC_SIG_LDFLAG_STR}#" ${PBC_SIG_CMAKE_PATH}
    PATCH_COMMAND && ${SED_CMMAND} -e "4s#^.*#${PBC_PBCSTRAP_COMMAND_STR}#" ${PBC_CMAKE_PATH}
    PATCH_COMMAND && ${SED_CMMAND} -e "5s#^.*#${PBC_SIG_CONFIG_COMMAND_STR}#" ${PBC_SIG_CMAKE_PATH}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCRYPTOPP_ROOT=<INSTALL_DIR>
    -DJSONCPP_ROOT=<INSTALL_DIR>
    -DARCH_NATIVE=${ARCH_NATIVE}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(GroupSigLib SOURCE_DIR)
set(LIB_SUFFIX .a)
set(DEPS_INCLUDE_DIR ${SOURCE_DIR}/deps/include)
file(MAKE_DIRECTORY ${DEPS_INCLUDE_DIR})

find_library(GMP_LIBRARIES NAMES "libgmp.a")
find_path(GMP_INCLUDE_DIR "gmp.h")
if(NOT GMP_INCLUDE_DIR)
    message(FATAL_ERROR "Please install libgmp first")
endif()
add_library(Gmp UNKNOWN IMPORTED)
set_property(TARGET Gmp PROPERTY IMPORTED_LOCATION ${GMP_LIBRARIES})
set_property(TARGET Gmp PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GMP_INCLUDE_DIR})

add_library(Pbc STATIC IMPORTED)
set_property(TARGET Pbc PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/deps/lib/libpbc${LIB_SUFFIX})
set_property(TARGET Pbc PROPERTY INTERFACE_LINK_LIBRARIES Gmp)
set_property(TARGET Pbc PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${DEPS_INCLUDE_DIR} ${GMP_INCLUDE_DIR})
add_dependencies(Pbc GroupSigLib)

add_library(PbcSig STATIC IMPORTED)
set_property(TARGET PbcSig PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/deps/lib/libpbc_sig${LIB_SUFFIX})
set_property(TARGET PbcSig PROPERTY INTERFACE_LINK_LIBRARIES Pbc)
set_property(TARGET PbcSig PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${DEPS_INCLUDE_DIR})
add_dependencies(PbcSig GroupSigLib)

add_library(GroupSig STATIC IMPORTED)
set(GROUPSIG_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libgroup_sig${LIB_SUFFIX})
set(GROUPSIG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/deps/include)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib)  # Must exist.
file(MAKE_DIRECTORY ${GROUPSIG_INCLUDE_DIR})  # Must exist.
set_property(TARGET GroupSig PROPERTY IMPORTED_LOCATION ${GROUPSIG_LIBRARY})
set_property(TARGET GroupSig PROPERTY INTERFACE_LINK_LIBRARIES PbcSig Pbc Gmp)
set_property(TARGET GroupSig PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GROUPSIG_INCLUDE_DIR} ${DEPS_INCLUDE_DIR})
add_dependencies(GroupSigLib Cryptopp JsonCpp)
add_dependencies(GroupSig GroupSigLib)
unset(SOURCE_DIR)
