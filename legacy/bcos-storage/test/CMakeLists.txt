# Legacy TiKV storage test (moved from bcos-storage/test/unittest)
if(WITH_TIKV)
    set(TEST_BINARY_NAME test-legacy-tikv-storage)

    add_executable(${TEST_BINARY_NAME} TestTiKVStorage.cpp main.cpp)
    target_include_directories(${TEST_BINARY_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-storage
        ${CMAKE_SOURCE_DIR}/bcos-table)

    find_package(Boost REQUIRED unit_test_framework log)
    find_package(fmt REQUIRED)
    find_package(RocksDB REQUIRED)

    target_link_libraries(${TEST_BINARY_NAME} PUBLIC
        storage-tikv table bcos-crypto RocksDB::rocksdb
        Boost::unit_test_framework Boost::log fmt::fmt bcos-legacy)

    add_test(NAME ${TEST_BINARY_NAME} COMMAND ${TEST_BINARY_NAME})
endif()
