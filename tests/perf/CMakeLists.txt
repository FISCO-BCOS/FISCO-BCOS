add_executable(storage_bench benchmark.cpp)
find_package(Boost REQUIRED program_options)
target_link_libraries(storage_bench PUBLIC ${CRYPTO_TARGET} ${STORAGE_TARGET} Boost::program_options)
