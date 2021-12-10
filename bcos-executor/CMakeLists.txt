cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

if (NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

# import bcos-cmake-scripts & hunter & bcos-framework
include(FetchContent)
set(BCOS_CMAKE_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts")
FetchContent_Declare(bcos-cmake-scripts
    DOWNLOAD_NO_PROGRESS 1
    GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-cmake-scripts.git
    GIT_TAG 2d25e8957323ffa86a55528c22348088ba99e9c6
    SOURCE_DIR ${BCOS_CMAKE_SCRIPTS_DIR}
    LOG_CONFIGURE 1
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_BUILD 1
    LOG_INSTALL 1
)

FetchContent_MakeAvailable(bcos-cmake-scripts)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" "${BCOS_CMAKE_SCRIPTS_DIR}")

# init hunter
set(HUNTER_PACKAGE_LOG_BUILD ON CACHE STRING "Hunter output build info to log" FORCE)
set(HUNTER_PACKAGE_LOG_INSTALL ON CACHE STRING "Hunter output install info to log" FORCE)
set(HUNTER_STATUS_DEBUG OFF CACHE STRING "Hunter debug output" FORCE)
set(HUNTER_USE_CACHE_SERVERS NO CACHE STRING "Download cache from cache server" FORCE)
set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")
# set(HUNTER_STATUS_PRINT OFF CACHE STRING "Hunter status output")
# set(CMAKE_CXX_STANDARD 17)

include(HunterGate)
HunterGate(
    URL "https://${URL_BASE}/FISCO-BCOS/hunter/archive/1030fc0469cc8d974b3a3d020ab2fd59df006edb.tar.gz"
    SHA1 "1bf2dc0631119c6304a9bac7445f1e77914c0fb7"
    FILEPATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake
)

# set CMP0074 to NEW to suppress warnning, https://cmake.org/cmake/help/latest/policy/CMP0074.html
cmake_policy(VERSION 3.12)
cmake_policy(SET CMP0074 NEW)

project(bcos-executor VERSION "3.0.0")

# basic setting
include(Options)
configure_project()
include(CompilerSettings)

include(InstallConfig)
# include(ProjectEVMC)
# include(ProjectEVMONE)
include(ProjectHera)
include(ProjectWABT)

# include(InstallBcosCryptoDependencies)
hunter_add_package(bcos-crypto)
find_package(wedpr-crypto CONFIG REQUIRED)
include(InstallBcosFrameworkDependencies)

include(InstallConfig)

function(install_dependencies target)
    install(
        TARGETS "${target}" EXPORT "${TARGETS_EXPORT_NAME}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    get_target_property(link_libraries ${target} LINK_LIBRARIES)
    foreach(lib ${link_libraries})
        get_target_property(type ${lib} TYPE)
        if(NOT type STREQUAL INTERFACE_LIBRARY)
            # get_property(lib_path TARGET ${lib} PROPERTY LOCATION)
            install(FILES $<TARGET_FILE:${lib}> DESTINATION ${CMAKE_INSTALL_LIBDIR})
            # get_target_property(LIB_INCLUDES ${lib} INTERFACE_INCLUDE_DIRECTORIES)
            # if(LIB_INCLUDES)
            #     install(DIRECTORY ${LIB_INCLUDES} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
            # endif()
        endif()
    endforeach()
endfunction()

add_subdirectory(src)

install(
    DIRECTORY "include/bcos-executor/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${CMAKE_PROJECT_NAME}"
    FILES_MATCHING PATTERN "*.h"
)

if (TOOLS)
    add_subdirectory(tools)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()
