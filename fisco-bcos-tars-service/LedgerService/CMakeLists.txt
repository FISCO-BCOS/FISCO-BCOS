cmake_minimum_required(VERSION 3.14)

project(bcostars-ledger)
file(GLOB SRC_LIST "*.cpp")

add_library(${LEDGER_SERVICE_LIB} ${SRC_LIST})
target_link_libraries(${LEDGER_SERVICE_LIB} ${TARS_PROTOCOL_TARGET})