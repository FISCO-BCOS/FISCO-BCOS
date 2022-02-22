file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BCOS_GATE_WAY_EXEC_TARGET "gateway-exec-mini")
add_executable(${BCOS_GATE_WAY_EXEC_TARGET} ${SRC_LIST})
target_link_libraries(${BCOS_GATE_WAY_EXEC_TARGET} PUBLIC  ${GATEWAY_TARGET} ${UTILITIES_TARGET} OpenSSL::SSL OpenSSL::Crypto ${FRONT_TARGET})
target_compile_options(${BCOS_GATE_WAY_EXEC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
