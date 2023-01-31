file(GLOB_RECURSE SOURCES "*.cpp")

find_package(benchmark REQUIRED)
add_executable(benchmark-level-storage ${SOURCES})
target_link_libraries(benchmark-level-storage transaction-scheduler transaction-executor bcos-framework benchmark::benchmark benchmark::benchmark_main)