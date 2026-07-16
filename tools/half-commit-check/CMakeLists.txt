find_package(Boost REQUIRED program_options)
find_package(RocksDB REQUIRED)

add_executable(half-commit-check main.cpp)
target_link_libraries(
    half-commit-check halfcommit-checker protocol-tars bcos-crypto Boost::program_options
    RocksDB::rocksdb)
target_include_directories(half-commit-check PRIVATE ${CMAKE_SOURCE_DIR})
