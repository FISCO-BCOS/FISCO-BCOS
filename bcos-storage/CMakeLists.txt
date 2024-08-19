cmake_minimum_required(VERSION 3.14)

project(bcos-storage VERSION ${VERSION})

find_package(zstd REQUIRED)
find_package(RocksDB REQUIRED)

find_package(Boost REQUIRED serialization thread context filesystem)

set(SRC_LIST bcos-storage/Common.cpp)
list(APPEND SRC_LIST bcos-storage/RocksDBStorage.cpp)

set(LIB_LIST ${TABLE_TARGET} bcos-framework Boost::serialization Boost::filesystem zstd::libzstd_static RocksDB::rocksdb ittapi)

if (WITH_TIKV)
    include(ProjectTiKVClient)
    list(APPEND SRC_LIST bcos-storage/TiKVStorage.cpp)
    list(APPEND LIB_LIST kv_client)
    # find_package(gRPC CONFIG REQUIRED)
    # list(APPEND LIB_LIST kv_client gRPC::gpr gRPC::upb gRPC::grpc gRPC::grpc++)
endif ()

add_library(${STORAGE_TARGET} ${SRC_LIST})
target_include_directories(${STORAGE_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-storage>)
target_link_libraries(${STORAGE_TARGET} PUBLIC ${LIB_LIST})
set_target_properties(${STORAGE_TARGET} PROPERTIES UNITY_BUILD "ON")

if (APPLE)
  target_link_libraries(${STORAGE_TARGET} PUBLIC "-framework SystemConfiguration")
endif()

if(TESTS)
  enable_testing()
  set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
  add_subdirectory(test)
endif()
