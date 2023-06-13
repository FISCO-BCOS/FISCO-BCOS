file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(benchmark-transaction-executor ${SOURCES})

find_package(benchmark REQUIRED)
target_link_libraries(benchmark-transaction-executor transaction-executor
    ${TARS_PROTOCOL_TARGET} bcos-framework evmone benchmark::benchmark benchmark::benchmark_main)