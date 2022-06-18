aux_source_directory(. SRC_LIST)

set(BINARY_NAME fisco-bcos-lightnode)
# add_executable(${BINARY_NAME} ${SRC_LIST})
# target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
# target_link_libraries(${BINARY_NAME} PUBLIC ${INIT_LIB} ${PBFT_INIT_LIB} ${TOOL_TARGET} ${COMMAND_HELPER_LIB} ${RPC_TARGET} ${GATEWAY_TARGET})
