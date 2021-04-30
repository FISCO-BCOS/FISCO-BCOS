# Cable: CMake Bootstrap Library.
# Copyright 2018 Pawel Bylica.
# Licensed under the Apache License, Version 2.0. See the LICENSE file.

set(cable_toolchain_dir ${CMAKE_CURRENT_LIST_DIR}/toolchains)

function(cable_configure_toolchain)
    if(NOT PROJECT_IS_NESTED)
        # Do this configuration only in the top project.

        cmake_parse_arguments("" "" "DEFAULT" ""  ${ARGN})

        set(default_toolchain default)
        if(_DEFAULT)
            set(default_toolchain ${_DEFAULT})
        endif()

        set(TOOLCHAIN ${default_toolchain} CACHE STRING "CMake toolchain")

        set(toolchain_file toolchains/${TOOLCHAIN}.cmake)
        foreach(path ${CMAKE_MODULE_PATH})
            if(EXISTS "${path}/${toolchain_file}")
                set(toolchain_file "${path}/${toolchain_file}")
                break()
            endif()
        endforeach()

        cable_debug("Toolchain file: ${toolchain_file}")
        set(CMAKE_TOOLCHAIN_FILE ${toolchain_file} CACHE FILEPATH "CMake toolchain file")
    endif()
endfunction()
