
project(bcostars-rpc)
file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

aux_source_directory(../ SRC_LIST)
add_executable(${RPC_BINARY_NAME} ${SRC_LIST})
target_link_libraries(${RPC_BINARY_NAME} protocol_init rpc ledger tool protocol_init command)