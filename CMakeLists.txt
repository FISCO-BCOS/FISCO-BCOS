cmake_minimum_required(VERSION 3.16)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(Options)

include(cmake/Version.cmake)
project(PROJECT_NAME VERSION ${VERSION})

configure_project()

include(CompilerSettings)
include(BuildInfoGenerator)

# import dependencies
include(ProjectWABT)
include(ProjectBCOSWASM)

find_package(TARS REQUIRED MODULE)
find_package(WeDPRCrypto REQUIRED MODULE)

# install dependencies
include(IncludeDirectories)

# define targets name
include(TargetSettings)

add_subdirectory(bcos-codec)
add_subdirectory(bcos-sealer)
add_subdirectory(bcos-security)
add_subdirectory(bcos-tool)
add_subdirectory(bcos-table)
add_subdirectory(bcos-framework)
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
add_subdirectory(bcos-utilities)
add_subdirectory(bcos-crypto)
add_subdirectory(bcos-boostssl)
add_subdirectory(libinitializer)
add_subdirectory(fisco-bcos-air)
add_subdirectory(fisco-bcos-tars-service)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
