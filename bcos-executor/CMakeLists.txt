cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

include(Version)
project(bcos-executor VERSION ${VERSION})

find_package(TBB CONFIG REQUIRED)
find_package(wedprcrypto REQUIRED)
# find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(evmc REQUIRED)
find_package(evmone REQUIRED)
find_package(Boost REQUIRED serialization program_options context)
find_package(jsoncpp REQUIRED)

file(GLOB_RECURSE SRCS src/*.cpp)
add_library(${EXECUTOR_TARGET} ${SRCS})
target_link_libraries(${EXECUTOR_TARGET} PUBLIC ${CODEC_TARGET} ${CRYPTO_TARGET} ${TABLE_TARGET}
    bcos-framework Boost::context evmone-standalone evmc::evmc fbwasm wabt wedprcrypto::fisco-bcos jsoncpp_static)

# add_subdirectory(test/trie-test)
# add_subdirectory(test/flow-graph)

if (TOOLS)
    add_subdirectory(tools)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test)
endif()
