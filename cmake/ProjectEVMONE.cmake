include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(evmone
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        DOWNLOAD_NAME evmone-92c8a886.tar.gz
        URL https://github.com/FISCO-BCOS/evmone/archive/92c8a8860b7e8cbb98dc3da4accb7ef8787de5f2.tar.gz
        URL_HASH SHA256=f39612d7e647bd40731d4683af3dc423e5ba65d0f5041c1a071cdb9b62d56bee
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=off
                   -DEVMC_ROOT=<INSTALL_DIR>
                   -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/src/.hunter
        # BUILD_COMMAND cmake --build . -- -j
        BUILD_IN_SOURCE 1
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a
)

ExternalProject_Get_Property(evmone INSTALL_DIR)
set(EVMONE_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${EVMONE_INCLUDE_DIRS})  # Must exist.
set(EVMONE_LIBRARIES ${INSTALL_DIR}/lib/libevmone.a ${INSTALL_DIR}/lib/libintx.a ${INSTALL_DIR}/lib/libkeccak.a)
add_library(EVMONE INTERFACE IMPORTED)
set_property(TARGET EVMONE PROPERTY INTERFACE_LINK_LIBRARIES ${EVMONE_LIBRARIES})
set_property(TARGET EVMONE PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${EVMONE_INCLUDE_DIRS})
add_dependencies(EVMONE evmone EVMC)
