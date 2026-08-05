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
# TODO(eth-utils-removal): 改为自研——rlp_encode→bcos-codec RLP,test_state→自研内存账本;
# 届时删除本段包源编译、portfile 的 vcpkg_replace_string 补丁与下方 shim。
# （mpt/mpt_hash 曾在此列，现已不随 port 安装，无需替换。）
get_target_property(_evmone_include_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_include_dirs 0 _evmone_include_dir)
cmake_path(NORMAL_PATH _evmone_include_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_include_dir}/test/utils")
if(NOT EXISTS "${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp")
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
# Refreshed on every configure, deliberately not guarded by an existence check. A symlink is
# preferred and stays live as eth/state changes; CREATE_LINK is idempotent and replaces a stale
# or dangling link cleanly. Where symlinks are unavailable (Windows without the privilege) the
# fallback is a directory copy, and that copy is a configure-time snapshot: guarding it with
# "already exists" would leave the first developer who edits eth/state building against stale
# headers — not a compile error, a wrong build. Re-copying each configure is cheap and removes
# that failure mode. (CREATE_LINK's COPY_ON_ERROR only handles regular files, not directories,
# so the fallback has to be spelled out.)
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/bcos-evm/eth/state"
    "${EVMONE_PKG_SHIM_DIR}/test/state" SYMBOLIC RESULT _shim_link_result)
if(NOT _shim_link_result STREQUAL "0")
    file(REMOVE_RECURSE "${EVMONE_PKG_SHIM_DIR}/test/state")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/bcos-evm/eth/state"
        DESTINATION "${EVMONE_PKG_SHIM_DIR}/test")
endif()

add_library(bcos-evm-eth STATIC
    bcos-evm/eth/state/block.cpp
    bcos-evm/eth/state/bloom_filter.cpp
    bcos-evm/eth/state/host.cpp
    bcos-evm/eth/state/precompiles.cpp
    bcos-evm/eth/state/requests.cpp
    bcos-evm/eth/state/state.cpp
    bcos-evm/eth/state/system_contracts.cpp
    # 自研内存账本（块级 PR 移植）：MemoryLedger 是 evmone test_state 的替换品，
    # StateRootCompute 是状态根引擎（AccountVisitor 建根，用 FISCO bcos-ledger/mpt
    # computeTrieRoot，不再依赖 evmone 的 MPT/mpt_hash）。
    bcos-evm/ledger/MemoryLedger.cpp
    bcos-evm/adapter/StateRootCompute.cpp
    # 只编入实际被引用的两个。blob_schedule/utils 曾在此，但全模块零引用
    # （符号可达性核验，且去掉后两个测试目标仍完整链接通过）；它们已不再随 port 安装。
    #
    # rlp_encode：块级回执建根（sealOpBlock 的 receipts-root leaf）用 evmone 编码作金值锚，
    # 属生产依赖。
    #
    # DEBT(eth-utils-removal): test_state 是 evmone 的内存**测试**账本。全部 11 个引用点都在
    # bcos-evm/test/ 下，两个生产库一个都不引用（nm 核验），所以它出现在生产库
    # bcos-evm-eth 里是不对的——bcosevm::eth 正是节点/执行器将要链接的那个库。
    #
    # 它没有被挪到测试 target，是因为那样解决不了根因：state::transition() 的声明是共享的，
    # 测试 target 编译 test_state.cpp 时同样会撞上上游的 7 参数调用，签名问题原样存在。真正
    # 的代价已经具体化过一次——为了让 chain id 参数保持「必需」而不是退回带默认值的
    # optional（那会让不安全语义成为 API 默认），ports/evmone/portfile.cmake 不得不用
    # vcpkg_replace_string 去改上游那一处调用点。一个上游测试助手就这样决定了共识函数的接口。
    #
    # 唯一干净的出口是块级 PR 里的自研内存账本：届时 test_state.{hpp,cpp} 不再需要安装，
    # 这一行、portfile 的补丁、以及下方的 shim 一并删除。在那之前这是一笔明确记账的债务，
    # 不是疏漏。
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp)
add_library(bcosevm::eth ALIAS bcos-evm-eth)
# Shim first: ${CMAKE_CURRENT_SOURCE_DIR} is <repo>/bcos-evm, which contains a real test/
# directory (test/eth, test/opstack). With it searched first, <test/state/...> and
# <test/utils/...> resolve to the shim and the package only for as long as bcos-evm/test/state/
# and bcos-evm/test/utils/ happen not to exist — adding either would silently shadow evmone's
# headers for every TU in the module, with no diagnostic. The shim holds nothing but test/, so
# putting it first costs nothing and removes the coincidence.
target_include_directories(bcos-evm-eth PUBLIC
    ${EVMONE_PKG_SHIM_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-eth PUBLIC evmone::evmone evmone::precompiles intx::intx)
# 自研建根(替代退役 evmone mpt/mpt_hash):StateRootCompute.h 用 bcos-codec RLP + bcos-ledger
# computeTrieRoot。`codec` 仅 in-tree 存在;`ledger` 由顶层 add_subdirectory(bcos-ledger) 提供
# (在 bcos-evm 之后,目标引用在 generate 期解析)。
if(TARGET codec)
    target_link_libraries(bcos-evm-eth PUBLIC codec)
endif()
target_link_libraries(bcos-evm-eth PUBLIC ledger)
target_include_directories(bcos-evm-eth PUBLIC ${CMAKE_SOURCE_DIR}/bcos-ledger)

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
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")

add_library(bcos-evm-opstack STATIC
    bcos-evm/opstack/OpForkSchedule.cpp
    bcos-evm/opstack/OpHost.cpp
    bcos-evm/opstack/OpPrecompiles.cpp
    bcos-evm/opstack/OpFeeParams.cpp
    bcos-evm/opstack/RollupCost.cpp
    bcos-evm/opstack/OpTransition.cpp
    # 块级执行层（op-validator-loop 移植）：processOpBlock / finalizeOpBlock / sealOpBlock。
    bcos-evm/opstack/OpBlockExecute.cpp
    bcos-evm/opstack/OpBlockFinalize.cpp
    bcos-evm/opstack/OpBlockSeal.cpp)
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
