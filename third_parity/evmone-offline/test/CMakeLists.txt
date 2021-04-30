# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2019 The evmone Authors.
# Licensed under the Apache License, Version 2.0.

include(GoogleTest)
include(${PROJECT_SOURCE_DIR}/deps/src/evmc/cmake/EVMC.cmake)

set(evmone_private_include_dir ${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(utils)
add_subdirectory(bench)
add_subdirectory(internal_benchmarks)
add_subdirectory(unittests)

set(targets evm-test evmone-bench evmone-bench-internal evmone-unittests testutils)

if(EVMONE_FUZZING)
    add_subdirectory(fuzzer)
    list(APPEND targets evmone-fuzzer)
endif()

set_target_properties(
    ${targets} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
)

get_target_property(type evmone TYPE)
if(type STREQUAL SHARED_LIBRARY)
    add_test(NAME ${PROJECT_NAME}/evm-test COMMAND evm-test $<TARGET_FILE:evmone>)
    evmc_add_vm_test(NAME ${PROJECT_NAME}/evmc-vmtester TARGET evmone)
endif()
