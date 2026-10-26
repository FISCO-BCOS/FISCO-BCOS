# Legacy TiKV storage test (moved from bcos-storage/test/unittest)
if(WITH_TIKV)
    set(TEST_BINARY_NAME test-legacy-tikv-storage)

    add_executable(${TEST_BINARY_NAME} TestTiKVStorage.cpp main.cpp)
    # main.cpp defines BOOST_TEST_MODULE; merged into a unity batch the boost.test
    # entry macro is disabled (TestTiKVStorage.cpp includes the header first),
    # breaking the link with undefined init_unit_test_suite.
    set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
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
