include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

set(TASSL_FOUND OFF)

add_library(TASSL MODULE IMPORTED)
if(NOT TASSL_ROOT_DIR)
  message(STATUS "Installing tassl from github")
  set(TASSL_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/tassl-install")
  make_directory(${TASSL_INSTALL}/include)

  ExternalProject_Add(tassl-project
    URL https://codeload.github.com/jntass/TASSL-1.1.1b/zip/63b602923f924b432774f6b6a2b22c708d5231c8
    URL_HASH SHA1=d4ffbdc5b29cf437f5f6711cc3d4b35f04b06965
    CONFIGURE_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tassl-project-prefix/src/tassl-project/config --prefix=${TASSL_INSTALL}
    BUILD_COMMAND make
    INSTALL_COMMAND make install || echo "Install ok"
  )

  set(TASSL_INCLUDE_DIRS "${TASSL_INSTALL}/include/")
  set(TASSL_LIBRARIES
    "${TASSL_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${TASSL_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(TASSL_ROOT_DIR ${TASSL_INSTALL})
  
  add_dependencies(TASSL tassl-project)
else()
  message(STATUS "Find tassl in ${TASSL_ROOT_DIR}")
  find_path(TASSL_INCLUDE_DIRS NAMES openssl PATHS ${TASSL_ROOT_DIR}/include REQUIRED)
  find_library(TASSL_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX} ${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${TASSL_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  message(STATUS "Found tassl include dir: ${TASSL_INCLUDE_DIRS} lib dir: ${TASSL_LIBRARIES}")
endif()

target_include_directories(TASSL INTERFACE ${TASSL_INCLUDE_DIRS})
set_property(TARGET TASSL PROPERTY IMPORTED_LOCATION ${TASSL_LIBRARIES})

set(TASSL_FOUND ON)