cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

project(bcos-executor VERSION "3.0.0")

aux_source_directory(src SRCS)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)

find_package(TBB CONFIG REQUIRED)
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(ProjectHera)
include(ProjectWABT)

hunter_add_package(evmc)
find_package(evmc CONFIG REQUIRED)
hunter_add_package(intx)
find_package(intx CONFIG REQUIRED)
hunter_add_package(ethash)
find_package(ethash CONFIG REQUIRED)
hunter_add_package(evmone)
find_package(evmone CONFIG REQUIRED)

add_library(executor ${SRCS})
target_link_libraries(executor framework TBB::tbb evmone hera hera-buildinfo evmc wabt intx ethash)

# if (TOOLS)
#     add_subdirectory(tools)
# endif()

# if (TESTS)
#     enable_testing()
#     set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
#     add_subdirectory(test)
# endif()
