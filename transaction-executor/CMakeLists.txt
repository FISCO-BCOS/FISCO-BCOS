cmake_minimum_required(VERSION 3.14)

find_package(evmc REQUIRED)
find_package(evmone REQUIRED)
find_package(intx REQUIRED)
find_package(ethash REQUIRED)
find_package(fmt REQUIRED)
find_package(jsoncpp REQUIRED)

add_library(transaction-executor
    bcos-transaction-executor/vm/VMInstance.cpp
    bcos-transaction-executor/precompiled/PrecompiledManager.cpp
    bcos-transaction-executor/precompiled/PrecompiledImpl.cpp
    bcos-transaction-executor/vm/HostContext.cpp
    bcos-transaction-executor/TransactionExecutorImpl.cpp
)
target_include_directories(transaction-executor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-transaction-executor>)
target_link_libraries(transaction-executor PUBLIC ${EXECUTOR_TARGET} jsoncpp_static ${CODEC_TARGET} ${CRYPTO_TARGET} ${TABLE_TARGET} bcos-protocol
    evmone evmc::loader evmc::instructions fmt::fmt-header-only)
set_target_properties(transaction-executor PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmark)
endif()