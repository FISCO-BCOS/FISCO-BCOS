# Gateway write-path benchmark (opt-in).
#
# Built only when GATEWAY_BENCH=ON so the default build is unaffected. It links the gateway library
# and reuses the same public headers a node does; nothing in bcos-gateway/ references it.
file(GLOB BENCH_SRCS "*.cpp")

set(GATEWAY_BENCH_TARGET "gateway-write-bench")
add_executable(${GATEWAY_BENCH_TARGET} ${BENCH_SRCS})

target_link_libraries(${GATEWAY_BENCH_TARGET} PUBLIC
    gateway
    bcos-utilities
    bcos-crypto
    protocol-tars
    bcos-task
    Boost::filesystem)

target_compile_options(${GATEWAY_BENCH_TARGET} PRIVATE -Wno-unused-variable -Wno-unused-parameter)
