include(ExternalProject)

if (APPLE)
    set(MYSQL_CLIENT_URL https://cdn.mysql.com/archives/mysql-connector-c/mysql-connector-c-6.1.11-macos10.12-x86_64.tar.gz)
    set(MYSQL_CLIENT_SHA256 c97d76936c6caf063778395e7ca15862770a1ab77c1731269408a8d5c0eb4b93)
else()
    set(MYSQL_CLIENT_URL https://cdn.mysql.com/archives/mysql-connector-c/mysql-connector-c-6.1.11-linux-glibc2.12-x86_64.tar.gz)
    set(MYSQL_CLIENT_SHA256 149102915ea1f1144edb0de399c3392a55773448f96b150ec1568f700c00c929)
endif()

ExternalProject_Add(MySQLClient
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME mysql-connector-c-6.1.11.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    BUILD_IN_SOURCE 1
    URL ${MYSQL_CLIENT_URL}
    URL_HASH SHA256=${MYSQL_CLIENT_SHA256}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND bash -c "cp lib/libmysqlclient.a ${CMAKE_SOURCE_DIR}/deps/lib/"
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libmysqlclient.a
)
ExternalProject_Get_Property(MySQLClient SOURCE_DIR)

set(MYSQL_CLIENT_LIB ${CMAKE_SOURCE_DIR}/deps/lib/libmysqlclient.a)
set(configure_command ./configure --with-mysql=${SOURCE_DIR}/bin/mysql_config --without-sqlite --without-postgresql --enable-shared=false --enable-protected)

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
set_property(TARGET zdb PROPERTY INTERFACE_LINK_LIBRARIES ${MYSQL_CLIENT_LIB} z)
add_dependencies(zdb libzdb MySQLClient)

unset(SOURCE_DIR)
