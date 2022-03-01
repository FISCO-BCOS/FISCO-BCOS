cmake_minimum_required(VERSION 3.16)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(Options)
include(HunterGate)
HunterGate(
        URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/a97876640bdca736c23a48dc0ad0f168fd3d695c.tar.gz"
        SHA1 "bb6f77268bfa8772ca30d5a4a62dda35840078ad"
        FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake"
)

include(cmake/Version.cmake)
project(PROJECT_NAME VERSION ${VERSION})

configure_project()

include(CompilerSettings)
include(BuildInfoGenerator)

# import dependencies
include(ProjectTCMalloc)
include(ProjectWABT)
include(ProjectBCOSWASM)

hunter_add_package(tbb)
hunter_add_package(Protobuf)
hunter_add_package(Microsoft.GSL)
hunter_add_package(wedpr-crypto)
hunter_add_package(jsoncpp)
hunter_add_package(OpenSSL)
hunter_add_package(evmc)
hunter_add_package(intx)
hunter_add_package(ethash)
hunter_add_package(evmone)
hunter_add_package(tarscpp)

# bcos-boostssl
hunter_add_package(bcos-boostssl)
find_package(bcos-boostssl CONFIG REQUIRED)
find_package(wedpr-crypto CONFIG REQUIRED)

# find OpenSSL
hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

# install dependencies
include(InstallBcosUtilities)
include(InstallBcosCrypto)
include(IncludeDirectories)

# define targets name
include(TargetSettings)

add_subdirectory(bcos-codec)
add_subdirectory(bcos-sealer)
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
add_subdirectory(libinitializer)
add_subdirectory(fisco-bcos-air)
add_subdirectory(fisco-bcos-pro)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
