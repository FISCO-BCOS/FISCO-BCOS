cmake_minimum_required(VERSION 3.15)

project(bcostars-front)
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-front)
include_directories(${CMAKE_SOURCE_DIR}/fisco-bcos-pro)

set(BINARY_NAME BcosFrontService)
aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST} ${HEADERS})
target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} front)