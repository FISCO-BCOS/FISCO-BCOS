cmake_minimum_required(VERSION 3.15)

project(bcostars-Gateway)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-gateway)
include_directories(${CMAKE_SOURCE_DIR}/fisco-bcos-pro)

find_package(tarscpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED program_options)

file(GLOB SRC_LIST "*.cpp")

set(BINARY_NAME BcosGatewayService)
aux_source_directory(../ SRC_LIST)
add_executable(${BINARY_NAME} ${SRC_LIST})

target_compile_options(${BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${BINARY_NAME} ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${GATEWAY_TARGET} Boost::program_options OpenSSL::SSL OpenSSL::Crypto)