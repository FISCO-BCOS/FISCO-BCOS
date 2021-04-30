# evmone: Ethereum Virtual Machine
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

cmake_minimum_required(VERSION 3.7)

# if(NOT EXISTS ${CMAKE_SOURCE_DIR}/evmc/.git)
#     message(FATAL_ERROR "Git submodules not initialized, execute:\n  git submodule update --init")
# endif()

option(BUILD_SHARED_LIBS "Build evmone as a shared library" ON)
option(EVMONE_TESTING "Build tests and test tools" OFF)
option(EVMONE_FUZZING "Instrument libraries and build fuzzing tools" OFF)

include(cmake/cable/bootstrap.cmake)
include(CableBuildType)
include(CableCompilerSettings)
include(CablePackage)
include(CableToolchains)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(ProjectEVMC)

cable_configure_toolchain(DEFAULT cxx17-pic)
cable_set_build_type(DEFAULT Release CONFIGURATION_TYPES Release Debug)

include(Hunter/init)

project(evmone LANGUAGES CXX)
set(PROJECT_VERSION 0.4.1)

string(REGEX MATCH "([0-9]+)\\.([0-9]+)" _ ${PROJECT_VERSION})
set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})

set(PROJECT_SOVERSION ${PROJECT_VERSION_MAJOR})
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(PROJECT_SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
endif()

hunter_add_package(ethash)
find_package(ethash CONFIG REQUIRED)

option(EVMC_TOOLS "Build EVMC test tools" ${EVMONE_TESTING})
option(EVMC_INSTALL "Install EVMC" OFF)
# add_subdirectory(evmc)

cable_configure_compiler()
if(CABLE_COMPILER_GNULIKE)
    add_compile_options(-Wno-attributes)  # Allow using unknown attributes.

    if(CABLE_COMPILER_CLANG)
        set(CMAKE_CXX_FLAGS_COVERAGE "-fprofile-instr-generate -fcoverage-mapping")
    elseif(CABLE_COMPILER_GNU)
        set(CMAKE_CXX_FLAGS_COVERAGE "--coverage")
    endif()
elseif(MSVC)
    add_compile_options(/wd4324)  # Disabled warning about alignment caused by alignas.
    add_compile_options(/wd5030)  # Allow using unknown attributes.
endif()

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

if(EVMONE_FUZZING)
    if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES Clang OR ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 8)
        message(FATAL_ERROR "Clang 8+ compiler is required for fuzzing")
    endif()

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build evmone as a shared library" FORCE)
    set(EVMONE_TESTING ON CACHE BOOL "Build tests and test tools" FORCE)

    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    string(COMPARE EQUAL ${build_type} COVERAGE fuzzing_coverage)
    if(NOT fuzzing_coverage)
        # Add fuzzing instrumentation only for non-coverage builds.
        # The coverage builds should be without fuzzing instrumentation to allow
        # running fuzzing corpus once and getting code coverage.
        set(fuzzing_flags -fsanitize=fuzzer-no-link,undefined,address)
        add_compile_options(${fuzzing_flags})
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${fuzzing_flags}")
    endif()
endif()

set(include_dir ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(lib)

if(EVMONE_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()


# INSTALL
# set(install_targets evmone)
# if(TARGET evmone-standalone)
#     list(APPEND install_targets evmone-standalone)
# endif()
# if(TARGET evm-test)
#     list(APPEND install_targets evm-test)
# endif()
# if(TARGET evmone-bench)
#     list(APPEND install_targets evmone-bench)
# endif()

# set_target_properties(
#     ${install_targets} PROPERTIES
#     ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
#     LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
# )
# message(STATUS ${install_targets})
# install(TARGETS ${install_targets} EXPORT evmoneTargets
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY ${include_dir}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

cable_add_archive_package()
