add_executable(storage_bench benchmark.cpp)
find_package(Boost CONFIG QUIET REQUIRED program_options)
target_link_libraries(storage_bench ${CRYPTO_TARGET} ${TABLE_TARGET} ${STORAGE_TARGET} Boost::program_options)
