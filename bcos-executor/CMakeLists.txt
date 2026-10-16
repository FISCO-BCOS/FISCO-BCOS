
project(bcos-executor VERSION ${VERSION})

find_package(TBB CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(Boost REQUIRED serialization program_options context)
find_package(jsoncpp REQUIRED)

file(GLOB_RECURSE SRCS src/*.cpp)
add_library(executor ${SRCS})
target_include_directories(executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
# NOTE: never link wedprcrypto::fisco-bcos (libffi_c_fisco_bcos.a) here or anywhere on the
# node link line: the archive bundles a stale vendored libsecp256k1 and, appearing before
# libsecp256k1.a, hijacks every secp256k1_* symbol in the final binary (its old
# secp256k1_ecdh ignores the hashfp argument and breaks the RLPx handshake with real
# geth/op-geth; ECDSA sign/verify/recover are silently bound to the unaudited copy too).
# Nothing references its entry points anymore: the alt_bn128 precompiles use
# evmmax::bn254 (Precompiled.cpp) since the evmone 0.21 upgrade (#5194).
target_link_libraries(executor PUBLIC jsoncpp_static ledger codec tool bcos-crypto table bcos-protocol
        Boost::context evmone::evmone GroupSig Paillier blst)
# Web3AccessListResolver decodes extraTransactionBytes via rpc::Web3Transaction RLP.
# TODO: move typed-tx RLP decode to bcos-codec to drop executor -> rpc dependency.
target_link_libraries(executor PRIVATE rpc)

set_source_files_properties(
    "src/vm/VMFactory.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# Exclude CoroutineTransactionExecutive from unity build to allow per-file
# ASan flags. boost::context coroutines trigger false-positive
# stack-use-after-scope errors under -fsanitize-address-use-after-scope.
set_source_files_properties(
    "src/executive/CoroutineTransactionExecutive.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TransactionExecutive.cpp has global-scope `using namespace bcos::codec;`,
# which leaks into later files of the same unity TU and makes `abi` ambiguous
# (bcos::codec::abi vs ::abi from <cxxabi.h>) in boost coroutine2 headers.
set_source_files_properties(
    "src/executive/TransactionExecutive.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# Files including range-v3 headers fail to compile when a global
# `using namespace std;` from an earlier file in the same unity TU is in
# effect (`ranges` becomes ambiguous with std::ranges). Build them standalone.
set_source_files_properties(
    "src/precompiled/ShardingPrecompiled.cpp"
    "src/precompiled/SystemConfigPrecompiled.cpp"
    "src/precompiled/common/WorkingSealerManagerImpl.cpp"
    "src/precompiled/extension/AccountManagerPrecompiled.cpp"
    "src/precompiled/extension/BalancePrecompiled.cpp"
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
