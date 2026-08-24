file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(benchmark-ethereum-executor ${SOURCES})

find_package(benchmark REQUIRED)
find_package(fmt REQUIRED)
# transaction-executor's include root exposes benchmark/BenchmarkERC20.h (the
# shared ERC-20 bytecode + account/transaction helpers).
target_include_directories(benchmark-ethereum-executor PRIVATE
    ${CMAKE_SOURCE_DIR}/transaction-executor)
target_link_libraries(benchmark-ethereum-executor ethereum-executor
    protocol-tars ledger bcos-framework codec evmone::evmone fmt::fmt
    benchmark::benchmark benchmark::benchmark_main)
set_target_properties(benchmark-ethereum-executor PROPERTIES UNITY_BUILD OFF)
