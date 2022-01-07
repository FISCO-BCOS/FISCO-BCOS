cmake_minimum_required(VERSION 3.15)

project(bcostars-scheduler)
file(GLOB SRC_LIST "*.cpp")

add_library(${SCHEDULER_SERVICE_LIB} ${SRC_LIST})
target_compile_options(${SCHEDULER_SERVICE_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${SCHEDULER_SERVICE_LIB} ${TARS_PROTOCOL_TARGET})