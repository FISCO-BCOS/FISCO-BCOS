cmake_minimum_required(VERSION 3.15)

project(bcostars-rpc)
file(GLOB SRC_LIST "*.cpp")

find_package(tarscpp CONFIG REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED program_options)

set(BINARY_NAME BcosRpcService)

aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST})
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} ${PROTOCOL_INIT_LIB} ${RPC_TARGET} ${TOOL_TARGET} ${PROTOCOL_INIT_LIB} ${COMMAND_HELPER_LIB})
