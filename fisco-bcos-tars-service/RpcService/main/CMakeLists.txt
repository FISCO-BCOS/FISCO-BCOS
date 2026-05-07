cmake_minimum_required(VERSION 3.14)

project(bcostars-rpc)
file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

aux_source_directory(../ SRC_LIST)
add_executable(${RPC_BINARY_NAME} ${SRC_LIST})
if(WITH_LEDGER_ELECTION)
    target_link_libraries(${RPC_BINARY_NAME} protocol_init rpc ledger tool protocol_init command leader_election)
else()
    target_link_libraries(${RPC_BINARY_NAME} protocol_init rpc ledger tool protocol_init command)
endif()