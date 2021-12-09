cmake_minimum_required(VERSION 3.15)

project(bcostars-txpool)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosTxPoolService)
aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} ${TXPOOL_INIT_LIB} bcos-framework::storage bcos-ledger::bcos-ledger bcos-framework::utilities bcos-framework::protocol)