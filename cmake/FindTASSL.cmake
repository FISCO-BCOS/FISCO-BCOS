include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

set(TASSL_FOUND OFF)

add_library(TASSL MODULE IMPORTED)
if(NOT TASSL_ROOT_DIR)
  message(STATUS "Installing wedpr-crypto from github")
  set(TASSL_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/tassl-install")
  make_directory(${TASSL_INSTALL}/include)

  ExternalProject_Add(wedpr-crypto
    URL https://codeload.github.com/jntass/TASSL-1.1.1b/zip/63b602923f924b432774f6b6a2b22c708d5231c8
    URL_HASH SHA1=dcbb69c96085ada1d107380b3771fd8e177ad207
    CONFIGURE_COMMAND ./config
    BUILD_COMMAND make
    INSTALL_COMMAND DESTDIR="${TASSL_INSTALL}" make install
  )

  set(TASSL_INCLUDE_DIRS "${TASSL_INSTALL}/include/")
  set(TASSL_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_crypto_binary${CMAKE_STATIC_LIBRARY_SUFFIX}")
  
  add_dependencies(WeDPRCrypto wedpr-crypto)
else()
  message(STATUS "Find wedpr-crypto in ${TASSL_ROOT_DIR}")
  find_path(TASSL_INCLUDE_DIRS NAMES wedpr-crypto PATHS ${TASSL_ROOT_DIR}/include REQUIRED)
  find_library(TASSL_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_crypto_binary${CMAKE_STATIC_LIBRARY_SUFFIX}}
    PATHS ${TASSL_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  message(STATUS "Found wedpr-crypto include dir: ${TASSL_INCLUDE_DIRS} lib dir: ${TASSL_LIBRARIES}")
endif()

target_include_directories(TASSL INTERFACE ${TASSL_INCLUDE_DIRS})
set_property(TARGET TASSL PROPERTY IMPORTED_LOCATION ${TASSL_LIBRARIES})

set(TASSL_FOUND ON)