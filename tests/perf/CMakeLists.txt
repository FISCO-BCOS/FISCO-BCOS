add_executable(storage-bench benchmark.cpp)
find_package(Boost REQUIRED program_options)
target_link_libraries(storage-bench ${CRYPTO_TARGET} ${TABLE_TARGET} ${STORAGE_TARGET} Boost::program_options)