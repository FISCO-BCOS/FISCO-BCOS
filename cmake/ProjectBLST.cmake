include(ExternalProject)
include(GNUInstallDirs)

find_program(BASH_COMMAND NAMES bash REQUIRED PATHS "/bin")
set(BLST_BUILD_COMMAND "${BASH_COMMAND}" build.sh)
set(BLST_LIB_NAME "${BASH_LIB_NAME}")

if (NOT BASH_COMMAND)
    message(FATAL_ERROR "bash not found")
endif ()

ExternalProject_Add(blst
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        GIT_REPOSITORY https://${URL_BASE}/supranational/blst.git
        GIT_TAG 3dd0f804b1819e5d03fb22ca2e6fac105932043a
        GIT_SHALLOW 0

        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${BLST_BUILD_COMMAND}
        INSTALL_COMMAND ""
        LOG_BUILD true
        LOG_INSTALL true
        LOG_CONFIGURE true
        BUILD_BYPRODUCTS <SOURCE_DIR>/libblst.a
        )

ExternalProject_Get_Property(blst INSTALL_DIR)

set(BLST_INCLUDE_DIR ${INSTALL_DIR}/include/)
file(MAKE_DIRECTORY ${BLST_INCLUDE_DIR})  #Must exist


set(BLST_LIBRARY ${INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/libblst.a)
add_library(BLST STATIC IMPORTED GLOBAL)

set_property(TARGET BLST PROPERTY IMPORTED_LOCATION ${BLST_LIBRARY})
set_property(TARGET BLST PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${BLST_INCLUDE_DIR})
add_dependencies(BLST blst)
unset(INSTALL_DIR)




