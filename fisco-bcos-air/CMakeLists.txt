aux_source_directory(. SRC_LIST)

find_package(tarscpp CONFIG REQUIRED)

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} PUBLIC init
front_init pbft_init txpool_init  protocol_init ${TOOL_TARGET} ${RPC_TARGET} ${GATEWAY_TARGET} framework tarscpp::tarsservant tarscpp::tarsutil bcos-boostssl::bcos-boostssl)
