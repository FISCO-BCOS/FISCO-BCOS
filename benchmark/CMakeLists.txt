find_package(Boost REQUIRED program_options)

add_executable(merkleBench merkleBench.cpp ParallelMerkleProof.cpp)
target_link_libraries(merkleBench tool bcos-crypto Boost::program_options)

find_package(fmt REQUIRED)
find_package(benchmark REQUIRED)
add_executable(benchmark-memory-storage benchmarkMemoryStorage.cpp)
target_link_libraries(benchmark-memory-storage bcos-framework benchmark::benchmark benchmark::benchmark_main fmt::fmt)

add_executable(benchmark-entry EntryBenchmark.cpp LegacyEntry.cpp)
target_link_libraries(benchmark-entry bcos-framework bcos-utilities benchmark::benchmark benchmark::benchmark_main)