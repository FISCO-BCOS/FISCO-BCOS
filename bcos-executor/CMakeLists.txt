
project(bcos-executor VERSION ${VERSION})

find_package(TBB CONFIG REQUIRED)
find_package(wedprcrypto REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(Boost REQUIRED serialization program_options context)
find_package(jsoncpp REQUIRED)

file(GLOB_RECURSE SRCS src/*.cpp)
add_library(executor ${SRCS})
target_include_directories(executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
if(WITH_WASM)
    target_link_libraries(executor PUBLIC jsoncpp_static ledger codec tool bcos-crypto table wedprcrypto::fisco-bcos bcos-protocol
            Boost::context evmone::evmone fbwasm wabt GroupSig Paillier blst)
else()
    target_link_libraries(executor PUBLIC jsoncpp_static ledger codec tool bcos-crypto table wedprcrypto::fisco-bcos bcos-protocol
            Boost::context evmone::evmone GroupSig Paillier blst)
endif()

set_source_files_properties(
    "src/vm/VMFactory.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(executor PROPERTIES UNITY_BUILD "ON")

if (TOOLS)
    add_subdirectory(tools)
endif()

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test/unittest)
endif()
