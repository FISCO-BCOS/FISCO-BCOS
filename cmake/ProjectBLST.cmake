include(ExternalProject)
include(GNUInstallDirs)

find_program(BASH_COMMAND NAMES bash REQUIRED PATHS "/bin")
set(BLST_BUILD_COMMAND "${BASH_COMMAND}" build.sh)

if (NOT BASH_COMMAND)
    message(FATAL_ERROR "bash not found")
endif ()

ExternalProject_Add(blst_project
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        GIT_REPOSITORY https://${URL_BASE}/supranational/blst.git
        GIT_TAG 3dd0f804b1819e5d03fb22ca2e6fac105932043a
        GIT_SHALLOW 0

        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${BLST_BUILD_COMMAND}
        INSTALL_COMMAND sh -c "cp <SOURCE_DIR>/bindings/blst.h <INSTALL_DIR>/include/  && cp <SOURCE_DIR>/bindings/blst_aux.h <INSTALL_DIR>/include/ && cp <SOURCE_DIR>/libblst.a <INSTALL_DIR>/lib/"
        LOG_BUILD true
        LOG_INSTALL true
        LOG_CONFIGURE true
        BUILD_BYPRODUCTS libblst.a
        )

ExternalProject_Get_Property(blst_project INSTALL_DIR)
add_library(blst STATIC IMPORTED)

set(BLST_LIBRARY ${INSTALL_DIR}/lib/libblst.a)
set(BLST_INCLUDE_DIR ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${INSTALL_DIR}/lib/)
file(MAKE_DIRECTORY ${BLST_INCLUDE_DIR})

set_property(TARGET blst PROPERTY IMPORTED_LOCATION ${BLST_LIBRARY})
set_property(TARGET blst PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${BLST_INCLUDE_DIR})
add_dependencies(blst blst_project)
unset(INSTALL_DIR)




