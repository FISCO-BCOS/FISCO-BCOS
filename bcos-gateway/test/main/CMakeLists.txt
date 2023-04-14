file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BCOS_GATE_WAY_EXEC_TARGET "gateway-exec-mini")
add_executable(${BCOS_GATE_WAY_EXEC_TARGET} main.cpp)
target_link_libraries(${BCOS_GATE_WAY_EXEC_TARGET} PUBLIC ${GATEWAY_TARGET} ${UTILITIES_TARGET} ${FRONT_TARGET})
target_compile_options(${BCOS_GATE_WAY_EXEC_TARGET} PRIVATE -Wno-unused-variable)

set(BCOS_GATE_WAY_ECHO_PERF_TARGET "p2p-echo-perf")
add_executable(${BCOS_GATE_WAY_ECHO_PERF_TARGET} p2p_echo_perf.cpp)
target_link_libraries(${BCOS_GATE_WAY_ECHO_PERF_TARGET} PUBLIC ${GATEWAY_TARGET} ${UTILITIES_TARGET} ${FRONT_TARGET})
target_compile_options(${BCOS_GATE_WAY_ECHO_PERF_TARGET} PRIVATE -Wno-unused-variable)
