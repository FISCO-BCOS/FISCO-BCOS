set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(. SRC_LIST)
aux_source_directory(./groupmgr SRC_LIST)

add_library(${JSONRPC_TARGET} ${SRC_LIST})
target_link_libraries(${JSONRPC_TARGET} PUBLIC bcos-framework::utilities ${WS_TARGET} bcos-tars-protocol::protocol-tars)