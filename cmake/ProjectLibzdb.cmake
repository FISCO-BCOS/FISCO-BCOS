include(ExternalProject)

set(configure_command ./configure --without-sqlite --without-postgresql)

ExternalProject_Add(libzdb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME libzdb-3.1.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://tildeslash.com/libzdb/dist/libzdb-3.1.tar.gz
    URL_HASH SHA256=0f01abb1b01d1a1f4ab9b55ad3ba445d203fc3b4757abdf53e1d85e2b7b42695
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ${configure_command} 
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/.libs/libzdb.a
)

ExternalProject_Get_Property(libzdb SOURCE_DIR)

add_library(zdb STATIC IMPORTED GLOBAL)
set(LIBZDB_INCLUDE_DIR ${SOURCE_DIR}/zdb/)
set(LIBZDB_LIBRARY ${SOURCE_DIR}/.libs/libzdb.a)
file(MAKE_DIRECTORY ${LIBZDB_INCLUDE_DIR})  # Must exist.


set_property(TARGET zdb PROPERTY IMPORTED_LOCATION ${LIBZDB_LIBRARY})
set_property(TARGET zdb PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${LIBZDB_INCLUDE_DIR})

add_dependencies(zdb libzdb)
unset(SOURCE_DIR)