aux_source_directory(. SRC_LIST)

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
target_link_libraries(${BINARY_NAME} PUBLIC ${INIT_LIB} ${PBFT_INIT_LIB} ${TOOL_TARGET} ${COMMAND_HELPER_LIB} ${RPC_TARGET} ${GATEWAY_TARGET})
