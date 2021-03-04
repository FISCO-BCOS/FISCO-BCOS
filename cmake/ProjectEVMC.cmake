include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(evmc
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        DOWNLOAD_NAME evmc-e0bd9d5d.tar.gz
        URL https://github.com/FISCO-BCOS/evmc/archive/d951b1ef088be6922d80f41c3c83c0cbd69d2bfa.tar.gz
        URL_HASH SHA256=96b7edd81f72d02936cd9632ca72bacc959d8ff2934edfe3486e01b813fbe39d
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        BUILD_IN_SOURCE 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmc-instructions.a <INSTALL_DIR>/lib/libevmc-loader.a
)

ExternalProject_Get_Property(evmc INSTALL_DIR)
set(EVMC_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${EVMC_INCLUDE_DIRS})  # Must exist.
add_library(EVMC::Loader STATIC IMPORTED)
set(EVMC_LOADER_LIBRARIES ${INSTALL_DIR}/lib/libevmc-loader.a)
set_property(TARGET EVMC::Loader PROPERTY IMPORTED_LOCATION ${EVMC_LOADER_LIBRARIES})
set_property(TARGET EVMC::Loader PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMC_INCLUDE_DIRS})

add_library(EVMC::Instructions STATIC IMPORTED)
set(EVMC_INSTRUCTIONS_LIBRARIES ${INSTALL_DIR}/lib/libevmc-instructions.a)
set_property(TARGET EVMC::Instructions PROPERTY IMPORTED_LOCATION ${EVMC_INSTRUCTIONS_LIBRARIES})
set_property(TARGET EVMC::Instructions PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMC_INCLUDE_DIRS})

add_library(EVMC INTERFACE IMPORTED)
set_property(TARGET EVMC PROPERTY INTERFACE_LINK_LIBRARIES ${EVMC_INSTRUCTIONS_LIBRARIES} ${EVMC_LOADER_LIBRARIES})
set_property(TARGET EVMC PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMC_INCLUDE_DIRS})
add_dependencies(EVMC evmc)
