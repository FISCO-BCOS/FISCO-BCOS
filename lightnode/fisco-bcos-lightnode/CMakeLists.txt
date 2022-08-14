cmake_minimum_required(VERSION 3.17)

add_executable(fisco-bcos-lightnode main.cpp)
target_link_libraries(fisco-bcos-lightnode PUBLIC bcos-cpp-sdk ${PROTOCOL_INIT_LIB} ${GATEWAY_TARGET} ${FRONT_TARGET} ${RPC_TARGET} ${STORAGE_TARGET} bcos-utilities ${TOOL_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-lightnode)