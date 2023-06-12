file(GLOB_RECURSE SOURCES "*.cpp")

find_package(fmt REQUIRED)

find_package(benchmark REQUIRED)
add_executable(benchmark-memory-storage ${SOURCES})
target_link_libraries(benchmark-memory-storage bcos-framework benchmark::benchmark benchmark::benchmark_main fmt::fmt-header-only)