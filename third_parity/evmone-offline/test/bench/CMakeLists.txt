# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2019 The evmone Authors.
# Licensed under the Apache License, Version 2.0.

include(CheckIncludeFileCXX)

hunter_add_package(benchmark)
find_package(benchmark CONFIG REQUIRED)

add_executable(evmone-bench bench.cpp)

target_include_directories(evmone-bench PRIVATE ${evmone_private_include_dir})
target_link_libraries(evmone-bench PRIVATE evmone testutils evmc::loader benchmark::benchmark)

set(HAVE_STD_FILESYSTEM 0)

if(NOT APPLE)
    # Check if <filesystem> (part of C++17 standard library) is available.
    # For macos assume not available. XCode 11 provides the header, but it can only be used
    # when building for macos 10.15.
    check_include_file_cxx(filesystem have_filesystem_include)
    if (have_filesystem_include)
        set(HAVE_STD_FILESYSTEM 1)
    endif()
endif()

target_compile_definitions(evmone-bench PRIVATE HAVE_STD_FILESYSTEM=${HAVE_STD_FILESYSTEM})

if(NOT HAVE_STD_FILESYSTEM)
    file(
        DOWNLOAD
        https://github.com/gulrak/filesystem/releases/download/v1.2.6/filesystem.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/filesystem.hpp
        EXPECTED_HASH SHA256=adc78f68ac26189c18679a87400d83b072f047f2ecd00b3ce6b1da16799dc217
    )
    target_include_directories(evmone-bench SYSTEM PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
elseif(UNIX AND NOT APPLE)
    target_link_libraries(evmone-bench PRIVATE stdc++fs)
endif()
