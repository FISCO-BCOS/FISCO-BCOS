# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

find_package(intx CONFIG REQUIRED)

add_library(testutils STATIC
    bytecode.hpp
    utils.cpp
    utils.hpp
)

target_link_libraries(testutils PRIVATE evmc::instructions)
target_include_directories(testutils PUBLIC ${PROJECT_SOURCE_DIR})

add_library(testutils-dump STATIC dump.cpp dump.hpp)
target_link_libraries(testutils-dump PRIVATE testutils evmone intx::intx)
target_include_directories(testutils-dump PRIVATE ${evmone_private_include_dir})
