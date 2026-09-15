
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
target_link_libraries(executor PUBLIC jsoncpp_static ledger codec tool bcos-crypto table wedprcrypto::fisco-bcos bcos-protocol
        Boost::context evmone::evmone GroupSig Paillier blst)

set_source_files_properties(
    "src/vm/VMFactory.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# Exclude CoroutineTransactionExecutive from unity build to allow per-file
# ASan flags. boost::context coroutines trigger false-positive
# stack-use-after-scope errors under -fsanitize-address-use-after-scope.
set_source_files_properties(
    "src/executive/CoroutineTransactionExecutive.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# Exclude files whose global `using namespace std;` / `using namespace bcos::codec;`
# would leak into sibling unity sources and break range-v3 / boost.coroutine2
# includes (ambiguous `ranges` / `abi` under GCC 14).
set_source_files_properties(
    "src/executive/BlockContext.cpp"
    "src/executive/ExecutiveDagFlow.cpp"
    "src/executive/TransactionExecutive.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
if(SANITIZE_ADDRESS)
    set_source_files_properties(
        "src/executive/CoroutineTransactionExecutive.cpp"
        PROPERTIES COMPILE_FLAGS "-fno-sanitize-address-use-after-scope")
endif()
set_target_properties(executor PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
    add_subdirectory(test/unittest)
endif()
