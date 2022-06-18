cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

include(Version)
project(bcos-storage VERSION ${VERSION})

find_package(zstd REQUIRED)
find_package(RocksDB REQUIRED)

find_package(Boost REQUIRED serialization thread context filesystem)

set(SRC_LIST src/Common.cpp)
list(APPEND SRC_LIST src/RocksDBStorage.cpp)

set(LIB_LIST ${TABLE_TARGET} bcos-framework Boost::serialization Boost::filesystem zstd::libzstd_static RocksDB::rocksdb)

if(TIKV)
    include(ProjectTiKVClient)
    list(APPEND SRC_LIST src/TiKVStorage.cpp)
    list(APPEND LIB_LIST kv_client)
endif()

if(NOT (TARGET zstd::zstd))
  # use zstd::libzstd_static instead zstd::zstd(depended by RocksDB)
  get_target_property(zstd_INCLUDE_DIRS zstd::libzstd_static INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(zstd_LIBRARIES zstd::libzstd_static LOCATION)
  add_library(zstd::zstd UNKNOWN IMPORTED)
  set_target_properties(zstd::zstd
    PROPERTIES
      IMPORTED_LOCATION ${zstd_LIBRARIES}
      INTERFACE_INCLUDE_DIRECTORIES ${zstd_INCLUDE_DIRS})
endif()

add_library(${STORAGE_TARGET} ${SRC_LIST})
target_link_libraries(${STORAGE_TARGET} PUBLIC ${LIB_LIST})

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()

option(TOOLS "build tools" OFF)
if(TOOLS)
    add_subdirectory(tools)
endif()