cmake_minimum_required(VERSION 3.14)

project(bcostars-Gateway)

include_directories(${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED program_options)

file(GLOB SRC_LIST "*.cpp")

aux_source_directory(../ SRC_LIST)
add_executable(${GATEWAY_BINARY_NAME} ${SRC_LIST})

# target_compile_options(${GATEWAY_BINARY_NAME} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
if(WITH_ETCD)
    target_link_libraries(${GATEWAY_BINARY_NAME} ${COMMAND_HELPER_LIB} ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${GATEWAY_TARGET} ${LEADER_ELECTION_TARGET} TCMalloc)
else()
    target_link_libraries(${GATEWAY_BINARY_NAME} bcos-crypto ${COMMAND_HELPER_LIB} ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${GATEWAY_TARGET} TCMalloc)
endif()
