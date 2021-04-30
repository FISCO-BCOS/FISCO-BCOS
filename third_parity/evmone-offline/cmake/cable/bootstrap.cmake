# Cable: CMake Bootstrap Library.
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

# Bootstrap the Cable - CMake Bootstrap Library by including this file.
# e.g. include(cmake/cable/bootstrap.cmake).


# Cable version.
#
# This is internal variable automatically updated with external tools.
# Use CABLE_VERSION variable if you need this information.
set(version 0.4.1)

# For convenience, add the project CMake module dir to module path.
set(module_dir ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
if(EXISTS ${module_dir})
    list(APPEND CMAKE_MODULE_PATH ${module_dir})
endif()

if(CABLE_VERSION)
    # Some other instance of Cable has been initialized in the top project.

    # Mark this project as nested.
    set(PROJECT_IS_NESTED TRUE)

    # Compare versions of the top project and this instances.
    if(CABLE_VERSION VERSION_LESS version)
        set(comment " (version older than ${version})")
    elseif(CABLE_VERSION VERSION_GREATER version)
        set(comment " (version newer than ${version})")
    endif()

    # Log information about initialization in the top project.
    file(RELATIVE_PATH subproject_dir ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    cable_debug("${subproject_dir}: Cable ${CABLE_VERSION}${comment} already initialized in the top project")
    cable_debug("Project CMake modules directory: ${module_dir}")

    unset(version)
    unset(module_dir)
    unset(comment)
    return()
endif()


option(CABLE_DEBUG "Enable Cable debug logs" OFF)

function(cable_log)
    message(STATUS "[cable ] ${ARGN}")
endfunction()

function(cable_debug)
    if(CABLE_DEBUG)
        message(STATUS "[cable*] ${ARGN}")
    endif()
endfunction()

# Export Cable version.
set(CABLE_VERSION ${version})

# Mark this project as non-nested.
set(PROJECT_IS_NESTED FALSE)

# Add Cable modules to the CMake module path.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

cable_log("Cable ${CABLE_VERSION} initialized")
cable_debug("Project CMake modules directory: ${module_dir}")

unset(version)
unset(module_dir)
