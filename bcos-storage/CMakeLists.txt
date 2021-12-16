cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

project(bcos-storage VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

option(USE_TiKV "build tikv storage or not" OFF)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)

set(SRC_LIST src/Common.cpp)
if(USE_TiKV)
    list(APPEND SRC_LIST src/TiKVStorage.cpp)
else()
    list(APPEND SRC_LIST src/RocksDBStorage.cpp)
endif()

hunter_add_package(zstd)
find_package(zstd CONFIG REQUIRED)
hunter_add_package(rocksdb)
find_package(RocksDB CONFIG REQUIRED)

find_package(Boost CONFIG QUIET REQUIRED serialization thread context)

add_library(storage ${SRC_LIST})
set_target_properties(storage PROPERTIES UNITY_BUILD ON)
target_link_libraries(storage PUBLIC framework Boost::serialization zstd::libzstd_static Threads::Threads)
if(USE_TiKV)
    target_link_libraries(storage PUBLIC kv_client)
else()
target_link_libraries(storage PUBLIC RocksDB::rocksdb)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
endif()

if(USE_TiKV)
    message(STATUS "USE_TiKV: ${USE_TiKV}")
    include(ProjectTiKVClient)
endif()

option(TOOLS "build tools" OFF)
if(TOOLS)
    add_subdirectory(tools)
endif()