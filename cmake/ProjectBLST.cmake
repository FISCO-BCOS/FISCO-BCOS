include(ExternalProject)
include(GNUInstallDirs)

find_program(BASH_COMMAND NAMES bash REQUIRED PATHS "/bin" "/opt/homebrew/bin")

if (NOT BASH_COMMAND)
    message(FATAL_ERROR "bash not found")
endif ()

set(BLST_CC "${CMAKE_C_COMPILER}")
if(CMAKE_OSX_SYSROOT)
    set(BLST_CC "${BLST_CC} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()
if(CMAKE_C_OSX_DEPLOYMENT_TARGET_FLAG AND CMAKE_OSX_DEPLOYMENT_TARGET)
    set(BLST_CC "${BLST_CC} ${CMAKE_C_OSX_DEPLOYMENT_TARGET_FLAG}${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
if(CMAKE_C_FLAGS)
    set(BLST_CC "${BLST_CC} ${CMAKE_C_FLAGS}")
endif()

set(BLST_BUILD_COMMAND
    "${BASH_COMMAND}" "-lc"
    "CC='${BLST_CC}' AR='${CMAKE_AR}' RANLIB='${CMAKE_RANLIB}' ./build.sh")

ExternalProject_Add(blst_project
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        GIT_REPOSITORY https://${URL_BASE}/supranational/blst.git
        GIT_TAG 52cc60d78591a56abb2f3d0bd1cdafc6ba242997
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



