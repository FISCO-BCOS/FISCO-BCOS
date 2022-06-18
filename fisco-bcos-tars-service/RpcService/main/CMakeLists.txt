cmake_minimum_required(VERSION 3.14)

project(bcostars-rpc)
file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

aux_source_directory(../ SRC_LIST)
add_executable(${RPC_BINARY_NAME} ${SRC_LIST})
target_compile_options(${RPC_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
if(ETCD)
    target_link_libraries(${RPC_BINARY_NAME} ${PROTOCOL_INIT_LIB} ${RPC_TARGET} ${TOOL_TARGET} ${PROTOCOL_INIT_LIB} ${COMMAND_HELPER_LIB} ${LEADER_ELECTION_TARGET})
else()
    target_link_libraries(${RPC_BINARY_NAME} ${PROTOCOL_INIT_LIB} ${RPC_TARGET} ${TOOL_TARGET} ${PROTOCOL_INIT_LIB} ${COMMAND_HELPER_LIB})
endif()