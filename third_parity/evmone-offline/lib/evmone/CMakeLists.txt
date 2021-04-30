# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

include(LibraryTools)

hunter_add_package(intx)
find_package(intx CONFIG REQUIRED)

add_library(evmone
    ${include_dir}/evmone/evmone.h
    analysis.cpp
    analysis.hpp
    evmone.cpp
    execution.cpp
    execution.hpp
    instructions.cpp
    limits.hpp
    opcodes_helpers.h
)
target_link_libraries(evmone PUBLIC intx::intx ethash::keccak)
target_link_libraries(evmone PRIVATE EVMC)
target_include_directories(evmone PUBLIC
    $<BUILD_INTERFACE:${include_dir}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(
    evmone
    PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_SOVERSION}
)

if(NOT SANITIZE)
    # On Linux, check if all symbols in evmone are resolved during linking.
    # target_link_options(evmone PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-undefined>)

endif()

set_source_files_properties(evmone.cpp PROPERTIES COMPILE_DEFINITIONS PROJECT_VERSION="${PROJECT_VERSION}")
install(TARGETS evmone EXPORT evmone
ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

get_target_property(link_libraries evmone LINK_LIBRARIES)
foreach(lib ${link_libraries})
    get_target_property(type ${lib} TYPE)
    if(NOT type STREQUAL INTERFACE_LIBRARY)
        get_property(lib_path TARGET ${lib} PROPERTY LOCATION)
        install(FILES ${lib_path} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endforeach()

# add_standalone_library(evmone)
