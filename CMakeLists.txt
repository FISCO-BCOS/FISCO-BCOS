cmake_minimum_required(VERSION 3.16)
if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/41051088cf9eb0abd0e33b4f1641727cbea57aff.tar.gz"
    SHA1 "8330e7840f53aaba6ef8b6996f5b1fd82f29d0b4"
    FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake"
)

include(cmake/Version.cmake)
project(PROJECT_NAME VERSION ${VERSION})


include(Options)
configure_project()

include(CompilerSettings)
include(BuildInfoGenerator)

# import dependencies
include(ProjectTCMalloc)
include(ProjectHera)
include(ProjectWABT)

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

# bcos-boostssl
hunter_add_package(bcos-boostssl)
find_package(bcos-boostssl CONFIG REQUIRED)

# find OpenSSL
hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

# install dependencies
include(InstallBcosUtilities)
include(IncludeDirectories)

# define targets name
include(TargetSettings)

add_subdirectory(bcos-crypto)
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

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# for code coverage
if (COVERAGE)
    include(Coverage)
    config_coverage("tars-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()
