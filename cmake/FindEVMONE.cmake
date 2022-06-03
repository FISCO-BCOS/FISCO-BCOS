include(FindPackageHandleStandardArgs)
include(FetchContent)
include(GNUInstallDirs)

add_library(evmone::evmone MODULE IMPORTED)
# Check found directory
if(NOT EVMONE_ROOT_DIR)
  message(STATUS "Install EVMONE from github")
  set(EVMONE_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/evmone-install)

  ExternalProject_Add(evmone-project
    URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/53ff1c54a2ee5ebcc499586da62ac6e1bb8735cd.tar.gz
    URL_HASH SHA1=e6c1a8f1acd908c770426bb5015d45b3f9138179
    PATCH_COMMAND sed -i "s/\#\\s*add_standalone_library/add_standalone_library/g" "${CMAKE_CURRENT_BINARY_DIR}/evmone-project-prefix/src/evmone-project/lib/evmone/CMakeLists.txt"
    CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=${EVMONE_INSTALL} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  )

  set(EVMONE_INCLUDE_DIRS "${EVMONE_INSTALL}/include")
  make_directory(${EVMONE_INCLUDE_DIRS})
  set(EVMONE_evmone_LIBRARIES
    "${EVMONE_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}evmone-standalone${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  
  add_dependencies(evmone::evmone evmone-project)
  set(EVMONE_ROOT_DIR ${EVMONE_INSTALL})
else()
  message(STATUS "Find EVMONE in ${EVMONE_ROOT_DIR}")
  find_path(EVMONE_INCLUDE_DIRS NAMES evmone PATHS ${EVMONE_ROOT_DIR}/include/ REQUIRED)
  find_library(EVMONE_evmone_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}evmone-standalone${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${EVMONE_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
endif()

target_include_directories(evmone::evmone INTERFACE ${EVMONE_INCLUDE_DIRS})
set_property(TARGET evmone::evmone PROPERTY IMPORTED_LOCATION ${EVMONE_evmone_LIBRARIES})

set(EVMONE_FOUND ON)