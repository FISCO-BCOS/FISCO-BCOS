file(GLOB_RECURSE SOURCES "*.cpp")

find_package(benchmark REQUIRED)
add_executable(benchmark-multilayer-storage ${SOURCES})
target_link_libraries(benchmark-multilayer-storage transaction-scheduler transaction-executor bcos-framework benchmark::benchmark benchmark::benchmark_main)