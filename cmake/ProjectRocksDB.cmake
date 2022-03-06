include(ExternalProject)

if (APPLE)
    set(SED_CMMAND sed -i .bkp)
    set(COMPILER_FLAGS "-Wno-defaulted-function-deleted -Wno-shadow")
else()
    set(SED_CMMAND sed -i)
    set(COMPILER_FLAGS "")
endif()

ExternalProject_Add(rocksdb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME rocksdb_6.29.3.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://codeload.github.com/facebook/rocksdb/tar.gz/v6.29.3
    URL_HASH SHA1=4c19f8f32a791e4fc186b780cf56ae489488810f
    # remove dynamic lib and gtest. NOTE: sed line number should update once RocksDB upgrade
    # PATCH_COMMAND ${SED_CMMAND} "s#-march=native#${MARCH_TYPE} ${COMPILER_FLAGS} #g" CMakeLists.txt COMMAND ${SED_CMMAND} "464d" CMakeLists.txt COMMAND ${SED_CMMAND} "739,749d" CMakeLists.txt COMMAND ${SED_CMMAND} "805,813d" CMakeLists.txt COMMAND ${SED_CMMAND} "s#-Werror##g" CMakeLists.txt
    PATCH_COMMAND ""
    BUILD_IN_SOURCE 1
    CMAKE_COMMAND ${CMAKE_COMMAND}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    ${_only_release_configuration}
    -DWITH_TESTS=OFF
    -DWITH_LZ4=OFF
    -DWITH_SNAPPY=ON
    -DWITH_GFLAGS=OFF
    -DWITH_TOOLS=OFF
    -DWITH_CORE_TOOLS=OFF
    -DWITH_JNI=OFF
    -DJNI=OFF
    -DROCKSDB_BUILD_SHARED=OFF
    -DWITH_BENCHMARK_TOOLS=OFF
    -DWITH_EXAMPLES=OFF
    -DUSE_RTTI=ON
    -DCMAKE_C_FLAGS='${MARCH_TYPE}'
    -DCMAKE_CXX_FLAGS='${MARCH_TYPE}'
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
