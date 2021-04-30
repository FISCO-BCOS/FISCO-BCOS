# evmone-fuzzer: LibFuzzer based testing tool for EVMC-compatible EVM implementations.
# Copyright 2019 The evmone Authors.
# Licensed under the Apache License, Version 2.0.
include(ExternalProject)

get_target_property(type evmone TYPE)
if(NOT type STREQUAL STATIC_LIBRARY)
    message(FATAL_ERROR "The evmone must be built as static library")
endif()

if(fuzzing_coverage)
    set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=fuzzer")
else()
    string(REPLACE fuzzer-no-link fuzzer CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
endif()

add_executable(evmone-fuzzer fuzzer.cpp)
target_link_libraries(evmone-fuzzer PRIVATE evmone testutils evmc::mocked_host)

if(NOT fuzzing_coverage)
    # TODO: Aleth reports undefined behaviors, disable it for fuzzing.
    string(REPLACE undefined "" aleth_fuzzing_flags ${fuzzing_flags})

    set(aleth_git_tag v1.7.0-rc.1)
    if(NOT aleth_git_tag STREQUAL "${aleth_current_git_tag}")
        message(STATUS "Aleth git tag has changed: ${aleth_current_git_tag} -> ${aleth_git_tag}")
        set(aleth_current_git_tag ${aleth_git_tag} CACHE INTERNAL "Current Aleth git tag" FORCE)
    else()
        message(STATUS "Aleth git tag: ${aleth_git_tag}; updates disabled")
        set(aleth_disable_git_update UPDATE_COMMAND "")
    endif()


    ExternalProject_Add(
        aleth-interpreter
        PREFIX external
        EXCLUDE_FROM_ALL TRUE

        GIT_REPOSITORY https://github.com/ethereum/aleth
        GIT_TAG ${aleth_git_tag}
        GIT_SUBMODULES evmc cmake/cable
        GIT_SHALLOW TRUE
        "${aleth_disable_git_update}"

        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_FLAGS=${aleth_fuzzing_flags}
        -DCMAKE_C_FLAGS=${aleth_fuzzing_flags}
        -DTOOLS=OFF
        -DTESTS=OFF

        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target aleth-interpreter
        INSTALL_COMMAND ""
    )

    set(build_dir ${CMAKE_CURRENT_BINARY_DIR}/external/src/aleth-interpreter-build)

    add_library(aleth::interpreter IMPORTED STATIC)
    add_dependencies(aleth::interpreter aleth-interpreter)
    set_target_properties(aleth::interpreter PROPERTIES
        IMPORTED_CONFIGURATIONS Release
        IMPORTED_LOCATION_RELEASE ${build_dir}/libaleth-interpreter/libaleth-interpreter.a
        INTERFACE_LINK_LIBRARIES ${build_dir}/aleth/libaleth-buildinfo.a)

    target_link_libraries(evmone-fuzzer PRIVATE aleth::interpreter)
    target_compile_definitions(evmone-fuzzer PRIVATE ALETH)
endif()