cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

# import bcos-cmake-scripts & hunter & bcos-framework
include(FetchContent)
set(BCOS_CMAKE_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/cmake/scripts")
FetchContent_Declare(bcos-cmake-scripts
    PREFIX ${CMAKE_SOURCE_DIR}/cmake/deps
    DOWNLOAD_NO_PROGRESS 1
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-cmake-scripts.git
    GIT_TAG 9b2ac5b979cbc97f640b2285feb2c74a18cfb935
    SOURCE_DIR ${BCOS_CMAKE_SCRIPTS_DIR}
    LOG_CONFIGURE 1
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_BUILD 1
    LOG_INSTALL 1
)

FetchContent_MakeAvailable(bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/ ${BCOS_CMAKE_SCRIPTS_DIR})

# init hunter
set(HUNTER_PACKAGE_LOG_BUILD ON CACHE STRING "Hunter output build info to log" FORCE)
set(HUNTER_PACKAGE_LOG_INSTALL ON CACHE STRING "Hunter output install info to log" FORCE)
set(HUNTER_STATUS_DEBUG OFF CACHE STRING "Hunter debug output" FORCE)
set(HUNTER_USE_CACHE_SERVERS NO CACHE STRING "Download cache from cache server" FORCE)
set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")
# set(HUNTER_STATUS_PRINT OFF CACHE STRING "Hunter status output")

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/203039fdd0826330facd9c3841082dbf35d0c808.tar.gz"
    SHA1 "26ca1840eb26da410913469f2f278569c8d383ae"
    FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake"
)

# set CMP0074 to NEW to suppress warnning, https://cmake.org/cmake/help/latest/policy/CMP0074.html
cmake_policy(VERSION 3.12)
cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0079 NEW)
cmake_policy(SET CMP0063 NEW)

project(bcos-storage VERSION "3.0.0")
set(VERSION_SUFFIX "-rc1")

# basic setting
include(Options)
configure_project()
include(CompilerSettings)
include(InstallConfig)

hunter_add_package(zstd)
find_package(zstd CONFIG REQUIRED)

# install bcos-framework
include(InstallBcosFrameworkDependencies)

option(USE_TiKV "build tikv storage or not" OFF)

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
endif()

if(USE_TiKV)
    message(STATUS "USE_TiKV: ${USE_TiKV}")
    include(ProjectTiKVClient)
endif()

set(STORAGE_TARGET "bcos-storage")
add_subdirectory(bcos-storage)

install(
    DIRECTORY "bcos-storage"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bcos-storage"
    FILES_MATCHING PATTERN "*.h"
)

option(TOOLS "build tools" OFF)
if(TOOLS)
    add_subdirectory(tools)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()
