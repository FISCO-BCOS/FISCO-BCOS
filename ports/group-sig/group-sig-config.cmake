# group-sig-config.cmake
# Creates plain imported targets compatible with original ExternalProject names

set(GROUPSIG_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(GROUPSIG_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib")
set(GROUPSIG_DEPS_INCLUDE_DIR "${GROUPSIG_INCLUDE_DIR}")

# GMP doesn't provide a CMake config in vcpkg, find it manually
find_library(GMP_LIBRARIES NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}gmp${CMAKE_STATIC_LIBRARY_SUFFIX}" REQUIRED)
find_path(GMP_INCLUDE_DIR "gmp.h" REQUIRED)
if(NOT TARGET Gmp)
    add_library(Gmp UNKNOWN IMPORTED GLOBAL)
    set_target_properties(Gmp PROPERTIES
        IMPORTED_LOCATION "${GMP_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${GMP_INCLUDE_DIR}"
    )
endif()

find_package(cryptopp CONFIG REQUIRED)

# Pbc
if(NOT TARGET Pbc)
    add_library(Pbc STATIC IMPORTED GLOBAL)
    set_target_properties(Pbc PROPERTIES
        IMPORTED_LOCATION "${GROUPSIG_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}pbc${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${GROUPSIG_DEPS_INCLUDE_DIR};${GMP_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "Gmp"
    )
endif()

# PbcSig
if(NOT TARGET PbcSig)
    add_library(PbcSig STATIC IMPORTED GLOBAL)
    set_target_properties(PbcSig PROPERTIES
        IMPORTED_LOCATION "${GROUPSIG_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}pbc_sig${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${GROUPSIG_DEPS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "Pbc"
    )
endif()

# GroupSig
if(NOT TARGET GroupSig)
    add_library(GroupSig STATIC IMPORTED GLOBAL)
    set_target_properties(GroupSig PROPERTIES
        IMPORTED_LOCATION "${GROUPSIG_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}group_sig${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${GROUPSIG_INCLUDE_DIR};${GROUPSIG_DEPS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "PbcSig;Pbc;Gmp;cryptopp::cryptopp"
    )
endif()
