# Cable: CMake Bootstrap Library.
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

if(cable_build_info_included)
    return()
endif()
set(cable_build_info_included TRUE)

include(GNUInstallDirs)

set(cable_buildinfo_template_dir ${CMAKE_CURRENT_LIST_DIR}/buildinfo)

function(cable_add_buildinfo_library)

    cmake_parse_arguments("" "" PROJECT_NAME;EXPORT "" ${ARGN})

    if(NOT _PROJECT_NAME)
        message(FATAL_ERROR "The PROJECT_NAME argument missing")
    endif()

    # Come up with the target and the C function names.
    set(name ${_PROJECT_NAME}-buildinfo)
    set(FUNCTION_NAME ${_PROJECT_NAME}_get_buildinfo)

    set(output_dir ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME})
    set(header_file ${output_dir}/buildinfo.h)
    set(source_file ${output_dir}/buildinfo.c)

    if(CMAKE_CONFIGURATION_TYPES)
        set(build_type ${CMAKE_CFG_INTDIR})
    else()
        set(build_type ${CMAKE_BUILD_TYPE})
    endif()

    # Find git here to allow the user to provide hints.
    find_package(Git)

    # Git info target.
    #
    # This target is named <name>-git and is always built.
    # The executed script gitinfo.cmake check git status and updates files
    # containing git information if anything has changed.
    add_custom_target(
        ${name}-git
        COMMAND ${CMAKE_COMMAND}
        -DGIT=${GIT_EXECUTABLE}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DOUTPUT_DIR=${output_dir}
        -P ${cable_buildinfo_template_dir}/gitinfo.cmake
        BYPRODUCTS ${output_dir}/gitinfo.txt
    )

    add_custom_command(
        COMMENT "Updating ${name}:"
        OUTPUT ${source_file} ${output_dir}/buildinfo.json
        COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_DIR=${output_dir}
        -DPROJECT_NAME=${_PROJECT_NAME}
        -DFUNCTION_NAME=${FUNCTION_NAME}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DSYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}
        -DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
        -DBUILD_TYPE=${build_type}
        -P ${cable_buildinfo_template_dir}/buildinfo.cmake
        DEPENDS
        ${cable_buildinfo_template_dir}/buildinfo.cmake
        ${cable_buildinfo_template_dir}/buildinfo.c.in
        ${cable_buildinfo_template_dir}/buildinfo.json.in
        ${name}-git
        ${output_dir}/gitinfo.txt
    )

    string(TIMESTAMP TIMESTAMP)
    configure_file(${cable_buildinfo_template_dir}/buildinfo.h.in ${header_file})

    # Add buildinfo library under given name.
    # Make is static and do not build by default until some other target will actually use it.
    add_library(${name} STATIC ${source_file} ${header_file})

    target_include_directories(${name} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    set_target_properties(
        ${name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${output_dir}
        ARCHIVE_OUTPUT_DIRECTORY ${output_dir}
    )

    if(_EXPORT)
        install(TARGETS ${name} EXPORT ${_EXPORT}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

endfunction()
