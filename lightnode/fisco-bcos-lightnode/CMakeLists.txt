cmake_minimum_required(VERSION 3.17)

file(GLOB_RECURSE SOURCES "bcos-lightnode/*.h")

add_executable(fisco-bcos-lightnode ${SOURCES} main.cpp)
target_link_libraries(fisco-bcos-lightnode PUBLIC bcos-cpp-sdk protocol_init gateway
    front rpc storage bcos-utilities
    command tool protocol-tars bcos-crypto bcos-lightnode ledger)