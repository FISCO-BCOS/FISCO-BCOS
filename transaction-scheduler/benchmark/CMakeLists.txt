find_package(benchmark REQUIRED)

add_executable(benchmark-multilayer-storage benchmarkMultiLayerStorage.cpp)
target_link_libraries(benchmark-multilayer-storage transaction-scheduler transaction-executor ${TARS_PROTOCOL_TARGET} bcos-framework benchmark::benchmark benchmark::benchmark_main)

add_executable(benchmark-scheduler benchmarkScheduler.cpp)
target_link_libraries(benchmark-scheduler transaction-scheduler transaction-executor ${STORAGE_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework benchmark::benchmark benchmark::benchmark_main)