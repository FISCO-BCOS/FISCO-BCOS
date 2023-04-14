include(ExternalProject)


ExternalProject_Add(ittapi_project
    PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps
    DOWNLOAD_NAME ittapi_v3.24.0.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://${URL_BASE}/intel/ittapi/archive/2de8a23f6130036dcd4d1b78d05df3187951d298.tar.gz
    URL_HASH SHA256=f230c07da3ea5063227794a7426d676829a2eb1890012a365ec27abda79d63ba
    BUILD_IN_SOURCE 1
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DITT_API_FORTRAN_SUPPORT=OFF
    INSTALL_COMMAND ""
    LOG_CONFIGURE 1
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_BYPRODUCTS <SOURCE_DIR>/bin/libittnotify.a <SOURCE_DIR>/bin/libadvisor.a <SOURCE_DIR>/bin/libjitprofiling.a
)

ExternalProject_Get_Property(ittapi_project SOURCE_DIR)

set(ITTAPI_INCLUDE_DIR ${SOURCE_DIR}/include)
set(ITTAPI_LIBRARY ${SOURCE_DIR}/bin/libittnotify.a)
file(MAKE_DIRECTORY ${ITTAPI_INCLUDE_DIR})  # Must exist.

if(WITH_VTUNE_ITT)
    add_library(ittapi STATIC IMPORTED GLOBAL)
    set_property(TARGET ittapi PROPERTY IMPORTED_LOCATION ${ITTAPI_LIBRARY})
else()
    add_library(ittapi INTERFACE IMPORTED GLOBAL)
endif()
set_property(TARGET ittapi PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${ITTAPI_INCLUDE_DIR})

add_dependencies(ittapi ittapi_project)
unset(SOURCE_DIR)
