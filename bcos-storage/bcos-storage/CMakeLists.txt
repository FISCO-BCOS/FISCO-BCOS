
include(GNUInstallDirs)
file(GLOB HEADERS "*.h")

# compile rocksDB
set(SRC_LIST Common.cpp RocksDBStorage.cpp)
if(USE_TiKV)
    list(APPEND SRC_LIST TiKVStorage.cpp)
endif()
# set(zstd_ROOT ${ZSTD_ROOT})
hunter_add_package(rocksdb)
find_package(RocksDB CONFIG REQUIRED)

add_library(${STORAGE_TARGET} ${SRC_LIST} ${HEADERS})

find_package(Boost CONFIG QUIET REQUIRED serialization thread context)
find_package(Threads REQUIRED)

# target_link_libraries(${STORAGE_TARGET} PUBLIC bcos::tikv_client TBB::tbb Boost::serialization RocksDB::rocksdb zstd::libzstd_static Threads::Threads bcos-framework::utilities)
target_link_libraries(${STORAGE_TARGET} PUBLIC TBB::tbb Boost::serialization RocksDB::rocksdb zstd::libzstd_static Threads::Threads bcos-framework::utilities)
if(USE_TiKV)
    target_link_libraries(${STORAGE_TARGET} PUBLIC kv_client)
endif()

install(
    TARGETS ${STORAGE_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bcos-storage"
)
