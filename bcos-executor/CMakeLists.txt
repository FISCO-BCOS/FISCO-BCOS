cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

project(bcos-executor VERSION "3.0.0")

file(GLOB_RECURSE SRCS src/*.cpp)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-executor/include)

find_package(TBB CONFIG REQUIRED)
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(ProjectHera)
include(ProjectWABT)

hunter_add_package(evmc)
hunter_add_package(evmone)
hunter_add_package(intx)
hunter_add_package(ethash)

find_package(evmc CONFIG REQUIRED)
find_package(evmone CONFIG REQUIRED)
find_package(intx CONFIG REQUIRED)
find_package(ethash CONFIG REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED serialization program_options context)

add_library(executor ${SRCS})
target_link_libraries(executor PUBLIC framework crypto Boost::context TBB::tbb evmone hera hera-buildinfo
${WASM_ENGINE_LIBRARY} evmc::loader evmc::instructions wabt)

if (TOOLS)
    add_subdirectory(tools)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()
