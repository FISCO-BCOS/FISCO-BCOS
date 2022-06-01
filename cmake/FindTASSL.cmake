include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

set(TASSL_FOUND OFF)

add_library(tassl::crypto MODULE IMPORTED)
add_library(tassl::ssl MODULE IMPORTED)
if(NOT TASSL_ROOT_DIR)
  message(STATUS "Installing tassl from github")
  set(TASSL_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/tassl-install")
  make_directory(${TASSL_INSTALL}/include)

  ExternalProject_Add(tassl-project
    URL https://${URL_BASE}/FISCO-BCOS/TASSL-1.1.1b/archive/f9d60fa510e5fbe24413b4abdf1ea3a48f9ee6aa.tar.gz
    URL_HASH SHA1=e56121278bf07587d58d154b4615f96575957d6f
    CONFIGURE_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tassl-project-prefix/src/tassl-project/config --prefix=${TASSL_INSTALL}
    BUILD_COMMAND make
    INSTALL_COMMAND make install || echo "Install ok"
  )

  set(TASSL_INCLUDE_DIRS "${TASSL_INSTALL}/include/")
  set(TASSL_crypto_LIBRARIES
    "${TASSL_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(TASSL_ssl_LIBRARIES
    "${TASSL_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(TASSL_ROOT_DIR ${TASSL_INSTALL})
  
  add_dependencies(tassl::crypto tassl-project)
  add_dependencies(tassl::ssl tassl-project)
else()
  message(STATUS "Find tassl in ${TASSL_ROOT_DIR}")
  find_path(TASSL_INCLUDE_DIRS NAMES openssl PATHS ${TASSL_ROOT_DIR}/include REQUIRED)
  find_library(TASSL_crypto_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX} 
    PATHS ${TASSL_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  find_library(TASSL_ssl_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${TASSL_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  message(STATUS "Found tassl include dir: ${TASSL_INCLUDE_DIRS} lib dir: ${TASSL_crypto_LIBRARIES} ${TASSL_ssl_LIBRARIES}")
endif()

target_include_directories(tassl::crypto INTERFACE ${TASSL_INCLUDE_DIRS})
set_property(TARGET tassl::crypto PROPERTY IMPORTED_LOCATION ${TASSL_crypto_LIBRARIES})

target_include_directories(tassl::ssl INTERFACE ${TASSL_INCLUDE_DIRS})
set_property(TARGET tassl::ssl PROPERTY IMPORTED_LOCATION ${TASSL_ssl_LIBRARIES})

set(TASSL_FOUND ON)