cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED URL_BASE)
    set(URL_BASE "github.com")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(VERSION "3.11.0")
set(VERSION_SUFFIX "")
include(Options)
configure_project()

if (WIN32)
# to fix boost log link error in Windows8 API
# https://github.com/microsoft/vcpkg/discussions/22762
#    add_definitions(-D_WIN32_WINNT=0x0601)
#    set(VCPKG_C_FLAGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
#    set(VCPKG_CXX_FLAGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
    add_definitions(-DBOOST_USE_WINAPI_VERSION=BOOST_WINAPI_VERSION_WIN7)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS "ON")
endif ()

# vcpkg init
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- vcpkg WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(FISCO-BCOS VERSION ${VERSION})

include(CompilerSettings)
include(BuildInfoGenerator)

include(IncludeDirectories)
include(TargetSettings)

add_subdirectory(bcos-boostssl)
add_subdirectory(bcos-framework)
add_subdirectory(bcos-crypto)
add_subdirectory(bcos-utilities)
add_subdirectory(concepts)
add_subdirectory(libtask)
add_subdirectory(bcos-tars-protocol)
add_subdirectory(bcos-protocol)
add_subdirectory(bcos-codec)
if(NOT ONLY_CPP_SDK)
    include(ProjectITTAPI)
endif ()

if(WITH_LIGHTNODE)
    add_subdirectory(lightnode)
endif()

if(FULLNODE)
    if(WITH_WASM)
        include(ProjectWABT)
        include(ProjectBCOSWASM)
    endif()
    include(ProjectGroupSig)
    include(ProjectPaillier)
    include(ProjectBLST)

    add_subdirectory(bcos-sealer)
    add_subdirectory(bcos-security)
    add_subdirectory(bcos-scheduler)
    add_subdirectory(bcos-executor)
    add_subdirectory(bcos-storage)
    add_subdirectory(bcos-ledger)
    add_subdirectory(bcos-rpc)
    add_subdirectory(bcos-gateway)
    add_subdirectory(bcos-pbft)
    add_subdirectory(bcos-rpbft)
    add_subdirectory(bcos-tool)
    add_subdirectory(bcos-table)
    add_subdirectory(bcos-txpool)
    add_subdirectory(bcos-sync)
    add_subdirectory(bcos-front)
    add_subdirectory(bcos-leader-election)
    add_subdirectory(libinitializer)
    add_subdirectory(fisco-bcos-air)

    add_subdirectory(transaction-executor)
    add_subdirectory(transaction-scheduler)

    if(WITH_TARS_SERVICES)
        add_subdirectory(fisco-bcos-tars-service)
    endif()

    if(TESTS)
        enable_testing()
        add_subdirectory(tests)
        add_subdirectory(benchmark)
    endif()
endif()

if(WITH_CPPSDK)
    add_subdirectory(bcos-sdk)
endif()

if(TOOLS)
  add_subdirectory(tools)
endif()

# for code coverage
if(COVERAGE)
    include(Coverage)
    set(TESTS ON)
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        set(CMAKE_CXX_FLAGS "-g --coverage ${CMAKE_CXX_FLAGS}")
        set(CMAKE_C_FLAGS "-g --coverage ${CMAKE_C_FLAGS}")
        set(CMAKE_SHARED_LINKER_FLAGS "--coverage ${CMAKE_SHARED_LINKER_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "--coverage ${CMAKE_EXE_LINKER_FLAGS}")
    elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        add_compile_options(-Wno-unused-command-line-argument)
        set(CMAKE_CXX_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_CXX_FLAGS}")
        set(CMAKE_C_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_C_FLAGS}")
    endif()
    config_coverage("coverage" "")
endif()

print_config("FISCO BCOS")

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/fiscobcos-config.cmake.in"
  "${CMAKE_BINARY_DIR}/fiscobcos-config.cmake"
  INSTALL_DESTINATION "share/fiscobcos"
)

install(
  FILES "${CMAKE_BINARY_DIR}/fiscobcos-config.cmake"
  DESTINATION "share/fiscobcos"
)

install(
    EXPORT fiscobcosTargets
    DESTINATION share/fiscobcos
    NAMESPACE fiscobcos::
)
# copy pre-commit to git hook

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit AND EXISTS ${CMAKE_SOURCE_DIR}/.git/hooks)
    file(COPY ${CMAKE_SOURCE_DIR}/tools/.ci/pre-commit DESTINATION ${CMAKE_SOURCE_DIR}/.git/hooks
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
