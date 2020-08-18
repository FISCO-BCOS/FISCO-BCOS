if(NOT WASM)
    return()
endif()
include(ExternalProject)
include(GNUInstallDirs)
find_program(CARGO_COMMAND cargo)
if(NOT CARGO_COMMAND)
    message(FATAL_ERROR "cargo/rustc is not installed")
endif()

ExternalProject_Add(hera
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/bxq2011hust/hera.git
        GIT_TAG 1177582b87b9af850666424dccd9a7f5e2a81182
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=OFF
                   -DHERA_WASMER=ON
                   -DHERA_DEBUGGING=${DEBUG}
                   -DEVMC_ROOT=<INSTALL_DIR>
                   -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/src/.hunter
        # BUILD_COMMAND cmake --build . -- -j
        BUILD_IN_SOURCE 1
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a <INSTALL_DIR>/lib/libhera-buildinfo.a <INSTALL_DIR>/lib/libwasmer_runtime_c_api.a
)

ExternalProject_Get_Property(hera INSTALL_DIR)
set(HERA_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${HERA_INCLUDE_DIRS})  # Must exist.
set(HERA_LIBRARIES ${INSTALL_DIR}/lib/libhera.a ${INSTALL_DIR}/lib/libhera-buildinfo.a ${INSTALL_DIR}/lib/libwasmer_runtime_c_api.a)
if(NOT APPLE)
    set(HERA_LIBRARIES ${HERA_LIBRARIES} rt)
endif()
add_library(HERA INTERFACE IMPORTED)
set_property(TARGET HERA PROPERTY INTERFACE_LINK_LIBRARIES ${HERA_LIBRARIES})
set_property(TARGET HERA PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HERA_INCLUDE_DIRS})
add_dependencies(hera EVMONE)
add_dependencies(HERA hera EVMC)
