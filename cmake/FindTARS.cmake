include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

add_library(tars::parse MODULE IMPORTED)
add_library(tars::servant MODULE IMPORTED)
add_library(tars::util MODULE IMPORTED)
# Check found directory
if(NOT TARS_ROOT_DIR)
  message(STATUS "Install TARS from github")
  set(TARS_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/tars-install)

  ExternalProject_Add(tars-project
    URL https://${URL_BASE}/FISCO-BCOS/TarsCpp/archive/5ef1e21daaf1e143e81be5c7560c879f76edf447.tar.gz
    URL_HASH SHA1=000a070a99d82740f2f238f2defbc2ee7ff3bf76
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${TARS_INSTALL} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  )

  set(TARS_INCLUDE_DIRS "${TARS_INSTALL}/include")
  make_directory(${TARS_INCLUDE_DIRS})
  set(TARS_parse_LIBRARIES
    "${TARS_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}tarsparse${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(TARS_servant_LIBRARIES
    "${TARS_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}tarsservant${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(TARS_util_LIBRARIES
    "${TARS_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}tarsutil${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )

  add_dependencies(tars::parse tars-project)
  add_dependencies(tars::servant tars-project)
  add_dependencies(tars::util tars-project)

  set(TARS_ROOT_DIR ${TARS_INSTALL})
else()
  message(STATUS "Find TARS in ${TARS_ROOT_DIR}")
  find_program(TARS_TARS2CPP NAMES tars2cpp PATHS ${TARS_ROOT_DIR}/tools/ REQUIRED)
  find_path(TARS_INCLUDE_DIRS NAMES framework servant PATHS ${TARS_ROOT_DIR}/include/ REQUIRED)
  find_library(TARS_parse_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}tarsparse${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${TARS_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  find_library(TARS_servant_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}tarsservant${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${TARS_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  find_library(TARS_util_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}tarsutil${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${TARS_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
endif()

set(TARS_TARS2CPP ${TARS_ROOT_DIR}/tools/tars2cpp)
target_include_directories(tars::parse INTERFACE ${TARS_INCLUDE_DIRS})
set_property(TARGET tars::parse PROPERTY IMPORTED_LOCATION ${TARS_parse_LIBRARIES})
target_include_directories(tars::servant INTERFACE ${TARS_INCLUDE_DIRS})
set_property(TARGET tars::servant PROPERTY IMPORTED_LOCATION ${TARS_servant_LIBRARIES})
target_include_directories(tars::util INTERFACE ${TARS_INCLUDE_DIRS})
set_property(TARGET tars::util PROPERTY IMPORTED_LOCATION ${TARS_util_LIBRARIES})

set(TARS_FOUND ON)