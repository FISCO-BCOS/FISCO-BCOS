cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

include(Version)
project(bcos-storage VERSION ${VERSION})

set(SRC_LIST src/Common.cpp)
list(APPEND SRC_LIST src/RocksDBStorage.cpp)
include(ProjectTiKVClient)
list(APPEND SRC_LIST src/TiKVStorage.cpp)

hunter_add_package(zstd)
find_package(zstd CONFIG REQUIRED)
hunter_add_package(rocksdb)
find_package(RocksDB CONFIG REQUIRED)

find_package(Boost CONFIG QUIET REQUIRED serialization thread context)

add_library(${STORAGE_TARGET} ${SRC_LIST})
target_link_libraries(${STORAGE_TARGET} PUBLIC ${TABLE_TARGET} Boost::serialization zstd::libzstd_static RocksDB::rocksdb kv_client ${SECURITY_TARGET})

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()

option(TOOLS "build tools" OFF)
if(TOOLS)
    add_subdirectory(tools)
endif()
