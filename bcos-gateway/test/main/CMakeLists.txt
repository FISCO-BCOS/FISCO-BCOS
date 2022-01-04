file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BCOS_GATE_WAY_EXEC_TARGET "gateway-exec-mini")
add_executable(${BCOS_GATE_WAY_EXEC_TARGET} ${SRC_LIST})
target_link_libraries(${BCOS_GATE_WAY_EXEC_TARGET} PUBLIC  ${GATEWAY_TARGET} bcos-framework::utilities OpenSSL::SSL OpenSSL::Crypto bcos-front::bcos-front)
target_compile_options(${BCOS_GATE_WAY_EXEC_TARGET} PRIVATE -Wno-error -Wno-unused-variable)
