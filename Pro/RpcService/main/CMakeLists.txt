cmake_minimum_required(VERSION 3.15)

project(bcostars-rpc)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

set(BINARY_NAME BcosRpcService)
hunter_add_package(bcos-boostssl)
find_package(bcos-boostssl CONFIG REQUIRED)

aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} ${PROTOCOL_INIT_LIB} bcos-rpc::rpc tarscpp::tarsservant tarscpp::tarsutil bcos-framework::storage bcos-framework::tool
bcos-boostssl::boostssl-websocket bcos-boostssl::boostssl-httpserver bcos-boostssl::boostssl-context)
