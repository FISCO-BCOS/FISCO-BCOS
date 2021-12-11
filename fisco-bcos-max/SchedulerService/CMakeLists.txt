cmake_minimum_required(VERSION 3.15)

project(bcostars-scheduler)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${SCHEDULER_SERVICE_LIB} ${SRC_LIST})
target_compile_options(${SCHEDULER_SERVICE_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${SCHEDULER_SERVICE_LIB} bcos-tars-protocol::protocol-tars tarscpp::tarsservant tarscpp::tarsutil)