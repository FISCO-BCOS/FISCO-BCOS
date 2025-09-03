find_package(Boost REQUIRED program_options)


find_package(benchmark REQUIRED)
add_executable(benchmark-any-holder benchmarkAnyHolder.cpp)
target_link_libraries(benchmark-any-holder ${TARS_PROTOCOL_TARGET} bcos-framework benchmark::benchmark benchmark::benchmark_main)