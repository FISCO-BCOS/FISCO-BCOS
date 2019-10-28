include(ExternalProject)

if (APPLE)
    set(SED_CMMAND sed -i .bkp)
else()
    set(SED_CMMAND sed -i)
endif()

ExternalProject_Add(rocksdb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME rocksdb_6.0.2.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://codeload.github.com/facebook/rocksdb/tar.gz/v6.0.2
    URL_HASH SHA256=89e0832f1fb00ac240a9438d4bbdae37dd3e52f7c15c3f646dc26887da16f342
    # remove dynamic lib and gtest. NOTE: sed line number should update once RocksDB upgrade
    PATCH_COMMAND ${SED_CMMAND} "s#-march=native#-march=x86-64 -mtune=generic -Wno-defaulted-function-deleted -Wno-shadow #g" CMakeLists.txt COMMAND ${SED_CMMAND} "464d" CMakeLists.txt COMMAND ${SED_CMMAND} "739,749d" CMakeLists.txt COMMAND ${SED_CMMAND} "805,813d" CMakeLists.txt
    BUILD_IN_SOURCE 1
    CMAKE_COMMAND ${CMAKE_COMMAND}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=on
    ${_only_release_configuration}
    -DWITH_LZ4=off
    -DWITH_SNAPPY=on
    -DWITH_GFLAGS=off
    -DWITH_TESTS=off
    -DWITH_TOOLS=off
    -DBUILD_SHARED_LIBS=Off
    -DUSE_RTTI=on
    -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} ${MARCH_TYPE}
    -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} ${MARCH_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    INSTALL_COMMAND ""
    LOG_CONFIGURE 1
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_BYPRODUCTS <SOURCE_DIR>/librocksdb.a
)

ExternalProject_Get_Property(rocksdb SOURCE_DIR)
add_library(RocksDB STATIC IMPORTED GLOBAL)

set(ROCKSDB_INCLUDE_DIR ${SOURCE_DIR}/include/)
set(ROCKSDB_LIBRARY ${SOURCE_DIR}/librocksdb.a)
file(MAKE_DIRECTORY ${ROCKSDB_INCLUDE_DIR})  # Must exist.

set_property(TARGET RocksDB PROPERTY IMPORTED_LOCATION ${ROCKSDB_LIBRARY})
set_property(TARGET RocksDB PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${ROCKSDB_INCLUDE_DIR})
set_property(TARGET RocksDB PROPERTY INTERFACE_LINK_LIBRARIES Snappy)

add_dependencies(rocksdb Snappy)
add_dependencies(RocksDB rocksdb)
unset(SOURCE_DIR)
