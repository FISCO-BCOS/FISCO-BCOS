include(ExternalProject)
include(GNUInstallDirs)
ExternalProject_Add(GroupSigLib
    PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps
    DOWNLOAD_NAME group_sig_lib-b8b9164.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://${URL_BASE}/FISCO-BCOS/group-signature-lib/archive/b9f3e2589b477c2cbe25472e3e30e49bf842a062.tar.gz
    URL_HASH SHA256=6f55ab3910053599f09f434c694f03f533757f92d10f6721c476725edcd3a052
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    LOG_PATCH 1
    LOG_DOWNLOAD 1
    BUILD_IN_SOURCE 1
    # Fix pbc_sig CMakeLists.txt: original tarball has backslash line continuations
    # in warnflags that produce invalid ninja build files.
    # 1) Copy fix script into GroupSigLib cmake/
    # 2) Modify cmake/ProjectPbcSig.cmake to call it after pbc_sig patch step
    # NOTE: MUST use "sh -c" because Ninja executes commands directly (not via
    # shell), so "&&" would be treated as a cp argument without it.
    PATCH_COMMAND sh -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/fix_pbc_sig_cmake.py cmake/ && python3 ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/inject_pbc_sig_fix.py"
    # Tell Ninja which files the external project produces so it can track them.
    BUILD_BYPRODUCTS
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}group_sig${CMAKE_STATIC_LIBRARY_SUFFIX}
        <SOURCE_DIR>/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc_sig${CMAKE_STATIC_LIBRARY_SUFFIX}
        <SOURCE_DIR>/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(GroupSigLib SOURCE_DIR)
set(DEPS_INCLUDE_DIR ${SOURCE_DIR}/deps/include)
file(MAKE_DIRECTORY ${DEPS_INCLUDE_DIR})

find_library(GMP_LIBRARIES NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}gmp${CMAKE_STATIC_LIBRARY_SUFFIX}")
find_path(GMP_INCLUDE_DIR "gmp.h")
if(NOT GMP_INCLUDE_DIR)
    message(FATAL_ERROR "Please install libgmp first")
endif()
add_library(Gmp UNKNOWN IMPORTED GLOBAL)
set_property(TARGET Gmp PROPERTY IMPORTED_LOCATION ${GMP_LIBRARIES})
set_property(TARGET Gmp PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GMP_INCLUDE_DIR})

add_library(Pbc STATIC IMPORTED GLOBAL)
set_property(TARGET Pbc PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET Pbc PROPERTY INTERFACE_LINK_LIBRARIES Gmp)
set_property(TARGET Pbc PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${DEPS_INCLUDE_DIR} ${GMP_INCLUDE_DIR})
add_dependencies(Pbc GroupSigLib)

add_library(PbcSig STATIC IMPORTED GLOBAL)
set_property(TARGET PbcSig PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc_sig${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET PbcSig PROPERTY INTERFACE_LINK_LIBRARIES Pbc)
set_property(TARGET PbcSig PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${DEPS_INCLUDE_DIR})
add_dependencies(PbcSig GroupSigLib)

add_library(GroupSig STATIC IMPORTED GLOBAL)
set(GROUPSIG_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}group_sig${CMAKE_STATIC_LIBRARY_SUFFIX})
set(GROUPSIG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/include)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/lib)  # Must exist.
file(MAKE_DIRECTORY ${GROUPSIG_INCLUDE_DIR})  # Must exist.

find_package(cryptopp CONFIG REQUIRED)
set_property(TARGET GroupSig PROPERTY IMPORTED_LOCATION ${GROUPSIG_LIBRARY})
set_property(TARGET GroupSig PROPERTY INTERFACE_LINK_LIBRARIES PbcSig Pbc Gmp cryptopp::cryptopp)
set_property(TARGET GroupSig PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GROUPSIG_INCLUDE_DIR} ${DEPS_INCLUDE_DIR})
add_dependencies(GroupSig GroupSigLib)
unset(SOURCE_DIR)
