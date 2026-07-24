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

add_library(bcos-evm-eth STATIC
    bcos-evm/adapter/StateRootCompute.cpp
    bcos-evm/eth/EthTransition.cpp
    bcos-evm/eth/state/block.cpp
    bcos-evm/eth/state/bloom_filter.cpp
    bcos-evm/eth/state/host.cpp
    bcos-evm/eth/state/precompiles.cpp
    bcos-evm/eth/state/requests.cpp
    bcos-evm/eth/state/state.cpp
    bcos-evm/eth/state/system_contracts.cpp
    bcos-evm/eth/utils/mpt.cpp
    bcos-evm/eth/utils/mpt_hash.cpp
    bcos-evm/eth/utils/rlp_encode.cpp
    bcos-evm/eth/utils/test_state.cpp
    bcos-evm/eth/utils/utils.cpp)
add_library(bcosevm::eth ALIAS bcos-evm-eth)
target_include_directories(bcos-evm-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-eth PUBLIC evmone::evmone evmone::precompiles intx::intx)

# 移植文件按上游原样编译；并入本仓后会继承全局 -Werror -Wall -Wextra -pedantic
# （cmake/CompilerSettings.cmake），AppleClang 对上游代码报 missing-field-initializers。
# vendored 代码不受本仓告警策略约束——抑制仅限移植源文件，FISCO 自有代码保持全 -Werror。
set_source_files_properties(
    bcos-evm/eth/state/block.cpp
    bcos-evm/eth/state/bloom_filter.cpp
    bcos-evm/eth/state/host.cpp
    bcos-evm/eth/state/precompiles.cpp
    bcos-evm/eth/state/requests.cpp
    bcos-evm/eth/state/state.cpp
    bcos-evm/eth/state/system_contracts.cpp
    bcos-evm/eth/utils/mpt.cpp
    bcos-evm/eth/utils/mpt_hash.cpp
    bcos-evm/eth/utils/rlp_encode.cpp
    bcos-evm/eth/utils/test_state.cpp
    bcos-evm/eth/utils/utils.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")

# 集成构建（主工程 add_subdirectory）下跟随主工程的 TESTS 开关，独立构建恒默认 ON；
# 否则整树 TESTS=ON 构建/CI 也不会编译 bcos-evm-eth-tests，回归无从暴露。
if(BCOS_EVM_STANDALONE OR TESTS)
    set(BCOS_EVM_TESTS_DEFAULT ON)
else()
    set(BCOS_EVM_TESTS_DEFAULT OFF)
endif()
option(BCOS_EVM_TESTS "Build bcos-evm tests" ${BCOS_EVM_TESTS_DEFAULT})
if(BCOS_EVM_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
