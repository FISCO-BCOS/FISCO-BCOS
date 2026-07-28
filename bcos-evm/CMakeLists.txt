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

# The evmone port installs test/utils sources verbatim (headers AND .cpp) under
# include/test/utils; compile the ones we use straight from the package instead of
# carrying a vendored copy.
# TODO(eth-utils-removal): C 路线下改为自研——mpt/mpt_hash→自研 MPT,rlp_encode→
# bcos-codec RLP,test_state→自研内存账本;届时删除本段包源编译与下方 shim。
get_target_property(_evmone_include_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_include_dirs 0 _evmone_include_dir)
cmake_path(NORMAL_PATH _evmone_include_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_include_dir}/test/utils")
if(NOT EXISTS "${EVMONE_TEST_UTILS_DIR}/mpt.cpp")
    message(FATAL_ERROR "evmone package does not ship test/utils sources; "
        "port-version >= 8 of the overlay port is required (${EVMONE_TEST_UTILS_DIR})")
endif()

# The packaged test/utils sources spell #include <test/state/...>; the package does not
# ship test/state (the state base stays vendored in bcos-evm/eth/state). Route that
# spelling to the vendored copy via a configure-time symlink shim in the build tree —
# no in-repo compat files. COPY_ON_ERROR degrades to a directory copy where symlinks
# are unavailable.
set(EVMONE_PKG_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/pkg-include-shim")
file(MAKE_DIRECTORY "${EVMONE_PKG_SHIM_DIR}/test")
if(NOT EXISTS "${EVMONE_PKG_SHIM_DIR}/test/state")
    file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/bcos-evm/eth/state"
        "${EVMONE_PKG_SHIM_DIR}/test/state" SYMBOLIC COPY_ON_ERROR)
endif()

add_library(bcos-evm-eth STATIC
    bcos-evm/eth/EthTransition.cpp
    bcos-evm/eth/state/block.cpp
    bcos-evm/eth/state/bloom_filter.cpp
    bcos-evm/eth/state/host.cpp
    bcos-evm/eth/state/precompiles.cpp
    bcos-evm/eth/state/requests.cpp
    bcos-evm/eth/state/state.cpp
    bcos-evm/eth/state/system_contracts.cpp
    ${EVMONE_TEST_UTILS_DIR}/blob_schedule.cpp
    ${EVMONE_TEST_UTILS_DIR}/mpt.cpp
    ${EVMONE_TEST_UTILS_DIR}/mpt_hash.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp
    ${EVMONE_TEST_UTILS_DIR}/utils.cpp)
add_library(bcosevm::eth ALIAS bcos-evm-eth)
target_include_directories(bcos-evm-eth PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EVMONE_PKG_SHIM_DIR})
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
    ${EVMONE_TEST_UTILS_DIR}/blob_schedule.cpp
    ${EVMONE_TEST_UTILS_DIR}/mpt.cpp
    ${EVMONE_TEST_UTILS_DIR}/mpt_hash.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp
    ${EVMONE_TEST_UTILS_DIR}/utils.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")

add_library(bcos-evm-opstack STATIC
    bcos-evm/opstack/OpForkSchedule.cpp
    bcos-evm/opstack/OpHost.cpp
    bcos-evm/opstack/OpPrecompiles.cpp
    bcos-evm/opstack/OpFeeParams.cpp
    bcos-evm/opstack/RollupCost.cpp
    bcos-evm/opstack/OpTransition.cpp
    bcos-evm/opstack/OpReceipt.cpp)
add_library(bcosevm::opstack ALIAS bcos-evm-opstack)
target_include_directories(bcos-evm-opstack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-opstack PUBLIC bcosevm::eth)

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
