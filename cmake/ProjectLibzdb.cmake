include(ExternalProject)

set(configure_command ./configure --without-sqlite --without-postgresql --enable-shared=false --enable-protected)

ExternalProject_Add(libzdb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME libzdb-3.2.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://tildeslash.com/libzdb/dist/libzdb-3.2.tar.gz
    URL_HASH SHA256=005ddf4b29c6db622e16303298c2f914dfd82590111cea7cfd09b4acf46cf4f2
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
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient PATHS /usr/lib64/mysql)
if (NOT MYSQL_CLIENT_LIB)
message(FATAL_ERROR "libmysqlclient can't find. Please install libmysqlclient-dev on Ubuntu, mysql-devel on CentOS")
endif()
set_property(TARGET zdb PROPERTY INTERFACE_LINK_LIBRARIES ${MYSQL_CLIENT_LIB} z)

add_dependencies(zdb libzdb)
unset(SOURCE_DIR)
