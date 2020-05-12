include(ExternalProject)

ExternalProject_Add(mysqlclient
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME mysql-connector-c-6.1.11-src.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    URL https://cdn.mysql.com/archives/mysql-connector-c/mysql-connector-c-6.1.11-src.tar.gz
        https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/mysql-connector-c-6.1.11-src.tar.gz
    URL_HASH SHA256=c8664851487200162b38b6f3c8db69850bd4f0e4c5ff5a6d161dbfb5cb76b6c4
    #please make sure MYSQL_TCP_PORT is set and not equal to 3306
    CMAKE_ARGS  -DMYSQL_TCP_PORT=3305 -DCMAKE_INSTALL_PREFIX=${CMAKE_SOURCE_DIR}/deps/
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libmysqlclient.a
)
ExternalProject_Get_Property(mysqlclient SOURCE_DIR)



set(MYSQL_CLIENT_LIB ${CMAKE_SOURCE_DIR}/deps/lib/libmysqlclient.a)
set(ZDB_CONFIGURE_COMMAND ./configure --with-mysql=${SOURCE_DIR}/scripts/mysql_config --without-sqlite --without-postgresql --enable-shared=false --enable-protected)

ExternalProject_Add(libzdb DEPENDS mysqlclient
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME libzdb-3.2.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://tildeslash.com/libzdb/dist/libzdb-3.2.tar.gz
        https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/libzdb-3.2.tar.gz
    URL_HASH SHA256=005ddf4b29c6db622e16303298c2f914dfd82590111cea7cfd09b4acf46cf4f2
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ${ZDB_CONFIGURE_COMMAND} 
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/.libs/libzdb.a
)
add_dependencies(libzdb mysqlclient)

ExternalProject_Get_Property(libzdb SOURCE_DIR)

add_library(zdb STATIC IMPORTED GLOBAL)
set(LIBZDB_INCLUDE_DIR ${SOURCE_DIR}/zdb/)
set(LIBZDB_LIBRARY ${SOURCE_DIR}/.libs/libzdb.a)
file(MAKE_DIRECTORY ${LIBZDB_INCLUDE_DIR})  # Must exist.

set_property(TARGET zdb PROPERTY IMPORTED_LOCATION ${LIBZDB_LIBRARY})
set_property(TARGET zdb PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${LIBZDB_INCLUDE_DIR})
set_property(TARGET zdb PROPERTY INTERFACE_LINK_LIBRARIES ${MYSQL_CLIENT_LIB} z)
add_dependencies(zdb libzdb mysqlclient)

unset(SOURCE_DIR)
