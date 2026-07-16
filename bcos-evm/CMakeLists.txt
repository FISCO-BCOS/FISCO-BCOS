cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_NAME)
    set(BCOS_EVM_STANDALONE ON)
else()
    set(BCOS_EVM_STANDALONE OFF)
endif()

project(bcos-evm LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm-eth STATIC bcos-evm/eth/EthTransition.cpp)
add_library(bcosevm::eth ALIAS bcos-evm-eth)
target_include_directories(bcos-evm-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-eth PUBLIC evmone::state evmone::evmone evmone::testutils)

add_library(bcos-evm-opstack STATIC
    bcos-evm/opstack/OpForkSchedule.cpp
    bcos-evm/opstack/OpHost.cpp
    bcos-evm/opstack/OpPrecompiles.cpp
    bcos-evm/opstack/OpFeeParams.cpp
    bcos-evm/opstack/OpPredeploys.cpp
    bcos-evm/opstack/RollupCost.cpp
    bcos-evm/opstack/OpValidate.cpp
    bcos-evm/opstack/OpTransition.cpp
    bcos-evm/opstack/OpExecCommon.cpp
    bcos-evm/opstack/OpDepositTx.cpp
    bcos-evm/opstack/OpReceiptMeta.cpp
    bcos-evm/opstack/OpBlockFinalize.cpp
)
add_library(bcosevm::opstack ALIAS bcos-evm-opstack)
target_include_directories(bcos-evm-opstack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-opstack PUBLIC bcosevm::eth)

option(BCOS_EVM_TESTS "Build bcos-evm tests" ${BCOS_EVM_STANDALONE})
if(BCOS_EVM_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
