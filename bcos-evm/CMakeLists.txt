cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_NAME)
    set(BCOS_EVM_STANDALONE ON)
else()
    set(BCOS_EVM_STANDALONE OFF)
endif()
if(NOT BCOS_EVM_STANDALONE)
    set(BCOS_EVM_STATE_ROOT_SOURCES bcos-evm/adapter/StateRootCompute.cpp)
endif()

project(bcos-evm LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(evmone CONFIG REQUIRED)

# The evmone port installs test/utils sources verbatim (headers AND .cpp) under
# include/test/utils; compile the ones we use straight from the package instead of
# carrying a vendored copy.
# TODO(eth-utils-removal): replace test_state with the in-house memory ledger, then drop this
# package-source compile, the portfile vcpkg_replace_string patch, and the shim below.
# (rlp_encode already migrated to bcos-codec + test target; mpt/mpt_hash no longer ship.)
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
    ${BCOS_EVM_STATE_ROOT_SOURCES}
    # 只编入实际被引用的两个。blob_schedule/utils 曾在此，但全模块零引用
    # （符号可达性核验，且去掉后两个测试目标仍完整链接通过）；它们已不再随 port 安装。
    #
    # rlp_encode: removed from the production lib (zero production callers). Its only consumer
    # was OpReceiptEncodeTest (ADL into evmone::state::rlp_encode), removed with the retired
    # OpReceipt.h; the receiptsRoot leaf encoding returns with block-seal wiring (part 3).
    #
    # DEBT(eth-utils-removal): test_state is evmone's in-memory test ledger; all 11 reference
    # points are under bcos-evm/test/, and neither production lib references it (nm-verified),
    # so compiling it into bcos-evm-eth — the lib nodes/executors link — is wrong.
    #
    # It can't just move to a test target: state::transition()'s declaration is shared, so a
    # test target hits the same upstream 7-arg signature problem. To keep the chain-id param
    # required (not an optional default that bakes unsafe semantics into the API), the portfile
    # has to vcpkg_replace_string an upstream call site — one test helper dictating a consensus
    # function's interface.
    #
    # The only clean exit is the block-level PR's in-house memory ledger: then test_state no
    # longer ships, and this line, the portfile patch, and the shim below all go. Until then
    # this is booked debt, not an oversight.
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
# computeTrieRoot。codec/ledger 均用 genex 解析(standalone 为空)；bcos-ledger 的头路径由
# ledger 目标的 PUBLIC include 传递（BUILD_INTERFACE 指向 <repo>/bcos-ledger），无需单独
# 添加——且 standalone 下 CMAKE_SOURCE_DIR 指向 bcos-evm 自身，该路径并不存在。
target_link_libraries(bcos-evm-eth PUBLIC $<$<TARGET_EXISTS:codec>:codec> $<$<TARGET_EXISTS:ledger>:ledger>)
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
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")

# opstack 库依赖 bcos-framework（OpTransition 投影到框架 receipt），standalone 不可编译——
# 仅 FULLNODE 集成构建包含。
if(NOT BCOS_EVM_STANDALONE)
    add_library(bcos-evm-opstack STATIC
        bcos-evm/opstack/OpForkSchedule.cpp
        bcos-evm/opstack/OpHost.cpp
        bcos-evm/opstack/OpPrecompiles.cpp
        bcos-evm/opstack/OpFeeParams.cpp
        bcos-evm/opstack/RollupCost.cpp
        bcos-evm/opstack/OpTransition.cpp)
    add_library(bcosevm::opstack ALIAS bcos-evm-opstack)
    target_include_directories(bcos-evm-opstack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(bcos-evm-opstack PUBLIC bcosevm::eth bcos-framework)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # OP modules use skipped-field designated initializers and GCC-13 reference-returning
        # helpers; downgrade only here (global -Wmissing-field-initializers stays active).
        target_compile_options(bcos-evm-opstack PRIVATE
            -Wno-error=missing-field-initializers -Wno-error=dangling-reference)
    endif()
endif()

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
