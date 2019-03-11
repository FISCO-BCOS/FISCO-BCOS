include(ExternalProject)

ExternalProject_Add(leveldb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME leveldb-1.20.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://codeload.github.com/google/leveldb/tar.gz/v1.20
    URL_HASH SHA256=f5abe8b5b209c2f36560b75f32ce61412f39a2922f7045ae764a2c23335b6664
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make out-static/libleveldb.a
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <SOURCE_DIR>/out-static/libleveldb.a
)

ExternalProject_Get_Property(leveldb SOURCE_DIR)
add_library(LevelDB STATIC IMPORTED GLOBAL)

set(LEVELDB_INCLUDE_DIR ${SOURCE_DIR}/include/)
set(LEVELDB_LIBRARY ${SOURCE_DIR}/out-static/libleveldb.a)
file(MAKE_DIRECTORY ${SOURCE_DIR}/out-shared)
file(MAKE_DIRECTORY ${LEVELDB_INCLUDE_DIR})  # Must exist.

set_property(TARGET LevelDB PROPERTY IMPORTED_LOCATION ${LEVELDB_LIBRARY})
set_property(TARGET LevelDB PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${LEVELDB_INCLUDE_DIR})
set_property(TARGET LevelDB PROPERTY INTERFACE_LINK_LIBRARIES Snappy)

add_dependencies(LevelDB leveldb)
unset(SOURCE_DIR)
