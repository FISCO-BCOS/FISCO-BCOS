cmake_minimum_required(VERSION 3.15)

project(bcostars-ledger)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${LEDGER_SERVICE_LIB} ${SRC_LIST})
target_compile_options(${LEDGER_SERVICE_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${LEDGER_SERVICE_LIB} bcos-tars-protocol::protocol-tars tarscpp::tarsservant tarscpp::tarsutil)