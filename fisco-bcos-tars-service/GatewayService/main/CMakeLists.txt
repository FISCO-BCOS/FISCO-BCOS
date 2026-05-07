cmake_minimum_required(VERSION 3.14)

project(bcostars-Gateway)

include_directories(${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED program_options)

file(GLOB SRC_LIST "*.cpp")

aux_source_directory(../ SRC_LIST)
add_executable(${GATEWAY_BINARY_NAME} ${SRC_LIST})

if(WITH_LEDGER_ELECTION)
    target_link_libraries(${GATEWAY_BINARY_NAME} command protocol_init tool gateway leader_election)
else()
    target_link_libraries(${GATEWAY_BINARY_NAME} bcos-crypto command protocol_init tool gateway)
endif()
