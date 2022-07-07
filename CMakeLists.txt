cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(Options)

include(cmake/Version.cmake)

# vcpkg init
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- vcpkg WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(PROJECT_NAME VERSION ${VERSION})

configure_project()

include(CompilerSettings)
include(BuildInfoGenerator)

# install dependencies
include(IncludeDirectories)

# define targets name
include(TargetSettings)

if(BOOSTSSL)
    add_subdirectory(bcos-boostssl)
endif()

if(FRAMEWORK)
    add_subdirectory(bcos-framework)
endif()

if(CRYPTO)
    add_subdirectory(bcos-crypto)
endif()

if(UTILITIES)
    add_subdirectory(bcos-utilities)
endif()

if(ALL_COMPONENTS)
    # import dependencies
    include(ProjectWABT)
    include(ProjectBCOSWASM)

    add_subdirectory(bcos-codec)
    add_subdirectory(bcos-sealer)
    add_subdirectory(bcos-security)
    add_subdirectory(bcos-tool)
    add_subdirectory(bcos-table)
    add_subdirectory(bcos-scheduler)
    add_subdirectory(bcos-executor)
    add_subdirectory(bcos-storage)
    add_subdirectory(bcos-ledger)
    add_subdirectory(bcos-protocol)
    add_subdirectory(bcos-tars-protocol)
    add_subdirectory(bcos-rpc)
    add_subdirectory(bcos-gateway)
    add_subdirectory(bcos-pbft)
    add_subdirectory(bcos-txpool)
    add_subdirectory(bcos-sync)
    add_subdirectory(bcos-front)
    add_subdirectory(bcos-leader-election)
    add_subdirectory(libinitializer)
    add_subdirectory(fisco-bcos-air)

    if(TARSSERVICES)
        add_subdirectory(fisco-bcos-tars-service)
    endif()

    add_subdirectory(concepts)
    add_subdirectory(lightnode)

    if(TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

# for code coverage
if(COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif()
