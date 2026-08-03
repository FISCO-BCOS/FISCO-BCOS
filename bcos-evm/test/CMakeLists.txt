find_package(Boost REQUIRED unit_test_framework)

add_executable(bcos-evm-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-eth-tests PRIVATE
    bcosevm::eth
    Boost::unit_test_framework
)
add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(bcos-evm-opstack-tests
    support/statetest_loader.cpp
    opstack/OpForkScheduleTest.cpp
    opstack/OpHostTest.cpp
    opstack/OpPrecompilesTest.cpp
    opstack/OpFeeParamsTest.cpp
    opstack/OpDepositTxTest.cpp
    opstack/OpPredeploysTest.cpp
    opstack/RollupCostTest.cpp
    opstack/OpValidateTest.cpp
    opstack/OpTransitionTest.cpp
    opstack/OpDepositTest.cpp
    opstack/OpBlockHarnessTest.cpp
    opstack/OpZeroDiffTest.cpp
    opstack/OpReceiptMetaTest.cpp
    opstack/OpReceiptMapTest.cpp
    opstack/Op7702Test.cpp
    opstack/OpFloorGasTest.cpp
    opstack/OpBlockFinalizeTest.cpp
    opstack/OpBlockExecuteTest.cpp
    opstack/OpStateDiffSanitizeTest.cpp
    opstack/OpReceiptEncodeTest.cpp
    opstack/OpBlockSealTest.cpp
    opstack/OpT8nReplayTest.cpp
    opstack/MemoryLedgerT8nReplayTest.cpp
    opstack/MemoryLedgerTest.cpp
)
target_compile_definitions(bcos-evm-opstack-tests PRIVATE
    EVM_REF_OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/vectors"
    OP_T8N_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/cases"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/golden/engine"
)
# support/statetest_loader.cpp is vendored from evmone's test/utils/ tree, where it sits
# alongside statetest.hpp/utils.hpp/stdx/ and includes them unqualified ("statetest.hpp").
# It now lives outside eth/utils/ (test-only, not part of bcosevm::eth), so its quoted
# includes need the vendored eth/utils/ directory on the search path to resolve.
# TODO(eth-utils-removal): 向量装载链路整体替换——statetest.hpp/statetest_loader.cpp/
# utils/blob_schedule 换自研 JSON→账本装载器后,本 include 路径与 loader 源一并删除;
# 15 个用 TestState 的测试文件与 OpT8nReplayTest 同步迁自研账本(见移除清单文档)。
target_include_directories(bcos-evm-opstack-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcos-evm/eth/utils
)
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)

# Storage2Ledger 桥测试仅在 in-tree(bcos-framework 目标存在,即根 CMakeLists.txt
# add_subdirectory(bcos-framework) 早于 add_subdirectory(bcos-evm))时编译;standalone
# 构建(bcos-evm/build,独立 vcpkg.json 不含 tbb/magic-enum/proxy/wedprcrypto 等
# bcos-framework 传递依赖)只交付 MemoryLedger 部分(plan Global Constraints,
# spec §7/§8 回填见 Task 7)。
if(TARGET bcos-framework)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/Storage2LedgerTest.cpp
        opstack/LedgerRootTest.cpp
        opstack/EbT8nReplayTest.cpp
        opstack/EthBlockHeaderTest.cpp
        opstack/OpSchedulerImplTest.cpp
        # op-validator-minimal-loop engine tests (Tasks 5a/5b/6). They drive
        # `bcos::engine::EngineServiceImpl` directly — the OP `newPayload` branch has no RPC
        # endpoint this cycle (spec §6.3/§6.4, 裁定 A6), so there is nothing else to drive.
        opstack/EngineVersionGateTest.cpp
        opstack/EngineOpBranchTest.cpp
        opstack/EngineNewPayloadGateTest.cpp
        # ── 编入与链 engine 库二选一(护栏,裁定 B2)────────────────────────────────────
        # The three files above `#include "engine/bcos-engine/EngineServiceImpl.h"`, whose
        # non-template helpers (detail::validateOpNewPayloadRequest / rebuildOpEthHeader /
        # toEthLogsBloom / narrowU256ToU64 / encodePayloadSequence / ...) live in
        # EngineServiceImpl.cpp. That .cpp is compiled straight into this test binary rather than
        # obtained by linking the `engine` CMake target.
        #
        # Pick ONE of the two — but note the guardrail is a CONVENTION, not something the build
        # enforces (final review B2-6 corrects the earlier claim that doing both "yields duplicate
        # symbols at link time"; static archives do not work that way). `engine` is a default
        # STATIC library with UNITY_BUILD ON, i.e. one archive member. A linker pulls an archive
        # member in only to resolve an *undefined* symbol; since this binary already defines those
        # symbols from the source entry below, the member is simply never extracted and adding
        # `engine` to target_link_libraries would link clean and SILENTLY. The failure mode is
        # therefore not a build error but a stale/duplicated definition nobody notices.
        #
        # Compiling the .cpp in is the chosen side, because linking `engine` would drag its PUBLIC
        # dependency closure (and, through the engine test precedent, `mempool`) into a bcos-evm
        # test binary that needs none of it. If you ever add `engine` to target_link_libraries
        # below, delete this source entry in the same edit — nothing will complain if you don't.
        ${CMAKE_SOURCE_DIR}/engine/bcos-engine/EngineServiceImpl.cpp)
    target_link_libraries(bcos-evm-opstack-tests PRIVATE bcos-framework)
    # Header search paths for the engine tests above (all in-tree-only, hence inside this gate):
    #   - ${CMAKE_SOURCE_DIR}          : resolves `#include "engine/bcos-engine/EngineServiceImpl.h"`
    #                                    — the same repo-root-on-the-include-path convention
    #                                    engine/test/CMakeLists.txt:12 already uses.
    #   - .../bcos-ledger              : EngineServiceImpl.h includes <bcos-ledger/LedgerMethods.h>.
    #   - .../transaction-scheduler    : the generic-composition-root zero-drift cases instantiate
    #                                    <bcos-transaction-scheduler/SchedulerSerialImpl.h>
    #                                    (header-only; no link edge needed).
    # These are spelled as raw source paths rather than taken from the `ledger` /
    # `transaction-scheduler` targets' usage requirements because the root CMakeLists.txt adds
    # both of those directories AFTER bcos-evm — so `if(TARGET ledger)` is false at this point,
    # even in a full in-tree build. (`target_link_libraries(... ledger)` below still works: CMake
    # resolves target names at generate time, so a target created later in the same project is
    # linked correctly.)
    #
    # KNOWN RISK, assessed and accepted (task-6 review M5, closed in task-7): putting the repo root
    # on the include path means every top-level entry becomes a candidate for an angle-bracket
    # include. The repo root does contain a directory named `concepts/`, which collides by name with
    # the C++20 standard header <concepts>. It is harmless in practice — the preprocessor's include
    # search skips directory entries and keeps looking, so <concepts> still resolves to the standard
    # library — and the whole in-tree target builds clean (206/206) as evidence. The exposure is
    # also bounded: these paths are PRIVATE to this one test target and never propagate. Should a
    # top-level *file* ever collide with a header name, the fix is to narrow these to the specific
    # subdirectories actually needed rather than to widen the guard.
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-ledger
        ${CMAKE_SOURCE_DIR}/transaction-scheduler)
    # `ledger` supplies LedgerMethods.cpp's non-template symbols (the templated
    # `getBlockNumber(storage, hash, fromStorage)` overload the OP branch uses is header-only, but
    # LedgerMethods.h's non-template declarations are not) — the same library the `engine` target
    # itself links PUBLIC (engine/CMakeLists.txt:14).
    target_link_libraries(bcos-evm-opstack-tests PRIVATE ledger)
    # bcos-tars-protocol (`protocol-tars`) and bcos-crypto: OpSchedulerImplTest.cpp needs a
    # concrete bcos::protocol::TransactionReceiptFactory/BlockHeader implementation
    # (bcostars::protocol::TransactionReceiptFactoryImpl/BlockHeaderImpl) and a CryptoSuite to
    # construct it, mirroring engine/test/unittests/engine/EngineServiceTest.cpp's own fixture
    # construction. Both targets are added by the root CMakeLists.txt unconditionally, ahead of
    # bcos-evm, under the same in-tree-build condition as bcos-framework's presence — gated here
    # rather than unconditionally for the same reason as the `codec` link below (op-validator
    # 闭环 Task 4, task-4-brief.md).
    target_link_libraries(bcos-evm-opstack-tests PRIVATE protocol-tars bcos-crypto)
    # bcos-codec's `codec` target only exists in an in-tree build (top-level
    # add_subdirectory(bcos-codec) precedes add_subdirectory(bcos-evm)) — same condition as
    # bcos-framework's presence, hence gated here rather than unconditionally (op-validator
    # 闭环 Task 3, task-3-brief.md). OpSchedulerImpl.h (Task 4) also depends on bcos-codec/rlp's
    # decode primitives (RLPDecode.h), reusing this same link edge.
    target_link_libraries(bcos-evm-opstack-tests PRIVATE codec)
endif()
