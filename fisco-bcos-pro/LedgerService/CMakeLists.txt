cmake_minimum_required(VERSION 3.15)

project(bcostars-ledger)
file(GLOB SRC_LIST "*.cpp")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-ledger)

add_library(${LEDGER_SERVICE_LIB} ${SRC_LIST})
target_compile_options(${LEDGER_SERVICE_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${LEDGER_SERVICE_LIB} protocol-tars tarscpp::tarsservant tarscpp::tarsutil)