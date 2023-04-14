cmake_minimum_required(VERSION 3.14)

project(bcos-transaction-executor VERSION ${VERSION})

find_package(evmc REQUIRED)
find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(ethash REQUIRED)

add_library(transaction-executor INTERFACE)
target_link_libraries(transaction-executor INTERFACE ${CODEC_TARGET} ${CRYPTO_TARGET} ${TABLE_TARGET} bcos-protocol
    evmone evmc::loader evmc::instructions)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()