include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

add_library(evmc::instructions MODULE IMPORTED)
add_library(evmc::loader MODULE IMPORTED)
# Check found directory
if(NOT EVMC_ROOT_DIR)
  message(STATUS "Install EVMC from github")
  set(EVMC_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/evmc-install)

  ExternalProject_Add(evmc-project
    URL https://${URL_BASE}/FISCO-BCOS/evmc/archive/2e8bc3a6fc9c3cdbf5440b329dcdf0753faee43c.tar.gz
    URL_HASH SHA1=51104be45aa5d87363700c2cfde132a1c0691043
    CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=${EVMC_INSTALL} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  )

  set(EVMC_INCLUDE_DIRS "${EVMC_INSTALL}/include")
  make_directory(${EVMC_INCLUDE_DIRS})
  set(EVMC_instructions_LIBRARIES
    "${EVMC_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}evmc-instructions${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(EVMC_loader_LIBRARIES
    "${EVMC_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}evmc-loader${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  
  add_dependencies(evmc::instructions evmc-project)
  add_dependencies(evmc::loader evmc-project)
  set(EVMC_ROOT_DIR ${EVMC_INSTALL})
else()
  message(STATUS "Find EVMC in ${EVMC_ROOT_DIR}")
  find_path(EVMC_INCLUDE_DIRS NAMES evmc PATHS ${EVMC_ROOT_DIR}/include/ REQUIRED)
  find_library(EVMC_instructions_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}evmc-instructions${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${EVMC_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  find_library(EVMC_loader_LIBRARIES NAMES
    ${CMAKE_STATIC_LIBRARY_PREFIX}evmc-loader${CMAKE_STATIC_LIBRARY_SUFFIX}
    PATHS ${EVMC_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
endif()

target_include_directories(evmc::instructions INTERFACE ${EVMC_INCLUDE_DIRS})
set_property(TARGET evmc::instructions PROPERTY IMPORTED_LOCATION ${EVMC_instructions_LIBRARIES})

target_include_directories(evmc::loader INTERFACE ${EVMC_INCLUDE_DIRS})
set_property(TARGET evmc::loader PROPERTY IMPORTED_LOCATION ${EVMC_loader_LIBRARIES})

set(EVMC_FOUND ON)