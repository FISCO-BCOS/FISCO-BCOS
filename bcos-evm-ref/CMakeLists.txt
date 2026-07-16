cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_NAME)
    set(BCOS_EVM_REF_STANDALONE ON)
else()
    set(BCOS_EVM_REF_STANDALONE OFF)
endif()

project(bcos-evm-ref LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm-ref-eth STATIC bcos-evm-ref/eth/EthTransition.cpp)
add_library(bcosevmref::eth ALIAS bcos-evm-ref-eth)
# PUBLIC include = 模块根（仓库主流双名布局），消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用；
# 编译期隔离不变：bcos-evm 不在 include path，越界 #include 仍编译错。
# 注意：本 include root 下另有 test/ 等目录，evmone 的 <test/state/…> 头依赖搜索顺序回落——
# 本模块 test/ 下不得新建 state/ 或 utils/ 子目录（Step 1 已核）。
target_include_directories(bcos-evm-ref-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-ref-eth PUBLIC evmone::state evmone::evmone evmone::testutils)

add_library(bcos-evm-ref-opstack STATIC
    bcos-evm-ref/opstack/OpForkSchedule.cpp
    bcos-evm-ref/opstack/OpHost.cpp
    bcos-evm-ref/opstack/OpPrecompiles.cpp
    bcos-evm-ref/opstack/OpFeeParams.cpp
    bcos-evm-ref/opstack/OpPredeploys.cpp
    bcos-evm-ref/opstack/RollupCost.cpp
    bcos-evm-ref/opstack/OpValidate.cpp
    bcos-evm-ref/opstack/OpTransition.cpp
    bcos-evm-ref/opstack/OpExecCommon.cpp
    bcos-evm-ref/opstack/OpDepositTx.cpp
    bcos-evm-ref/opstack/OpReceiptMeta.cpp
    bcos-evm-ref/opstack/OpBlockFinalize.cpp
)
add_library(bcosevmref::opstack ALIAS bcos-evm-ref-opstack)
# PUBLIC include = 模块根（仓库主流双名布局），消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用；
# 编译期隔离不变：bcos-evm 不在 include path，越界 #include 仍编译错。
# 注意：本 include root 下另有 test/ 等目录，evmone 的 <test/state/…> 头依赖搜索顺序回落——
# 本模块 test/ 下不得新建 state/ 或 utils/ 子目录（Step 1 已核）。
target_include_directories(bcos-evm-ref-opstack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-ref-opstack PUBLIC bcosevmref::eth)

option(BCOS_EVM_REF_TESTS "Build bcos-evm-ref tests" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# M3.5 Phase 1 spike：读放大测量工具（非测试目标，不进 ctest）
option(BCOS_EVM_REF_SPIKE "Build bcos-evm-ref spike tools" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_SPIKE)
    add_executable(bcos-evm-ref-read-amplification spike/ReadAmplification.cpp)
    target_link_libraries(bcos-evm-ref-read-amplification PRIVATE bcosevmref::eth)
endif()
