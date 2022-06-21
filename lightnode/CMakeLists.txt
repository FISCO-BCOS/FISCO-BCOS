aux_source_directory(. SRC_LIST)

add_executable(lightnode ${SRC_LIST})
target_compile_options(lightnode PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(lightnode PUBLIC ${INIT_LIB} ${PBFT_INIT_LIB} ${TOOL_TARGET} ${COMMAND_HELPER_LIB} ${RPC_TARGET} ${GATEWAY_TARGET})