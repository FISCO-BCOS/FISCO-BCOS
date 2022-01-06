cmake_minimum_required(VERSION 3.15)

project(bcostars-ledger)
file(GLOB SRC_LIST "*.cpp")

add_library(${LEDGER_SERVICE_LIB} ${SRC_LIST})
target_compile_options(${LEDGER_SERVICE_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${LEDGER_SERVICE_LIB} ${TARS_PROTOCOL_TARGET} tarscpp::tarsservant tarscpp::tarsutil)