include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(GroupSigLib
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME group_sig_lib-b8b9164.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://github.com/FISCO-BCOS/group-signature-lib/archive/8c30f008e1399afc5be8877231f92a5d90822ad5.tar.gz
        https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/group_sig_lib-b8b9164.tar.gz
    URL_HASH SHA256=ea4cecd6c4cf300d661d006fa870151e84990fe757446738054c70046f0524d5
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
