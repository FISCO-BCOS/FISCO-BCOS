# OP Stack EL Engine/RPC 表面对齐实施计划（audit v2：BL-3 / MJ-1 / MJ-2 + RPC 小修）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 OP Stack EL spec 对齐审计 v2（`docs/2026-08-21-opstack-el-spec-audit-v2.md`）中自包含的 Engine/RPC 表面对齐缺口：BL-3（Jovian minBaseFee 构建侧）、MJ-1（engine 错误码映射）、MJ-2（FCU attrs 深度校验）、MN-3/4/6/7/8（RPC 与工具链小修）。

**Architecture:** 三块独立改动：① 引擎异常类型从 `EngineServiceImpl.h` 下沉到 `bcos-framework/engine/Errors.h`（rpc 与 engine 均可命名，避免 rpc→engine 循环依赖），`bcos-rpc` 新增 `EngineErrorMapper.h` 做异常→execution-apis 错误码映射，`EngineEndpoint` 三个 handler 统一转换；② `updateForkchoice` 的 OP 分支启用 attrs 深度校验（gasLimit/eip1559Params/minBaseFee 必填语义，对齐 op-geth `checkOptimismPayloadAttributes`），失败返回 STATUS_INVALID 而非静默规整；③ `buildOpPayload` 的 Jovian extraData lambda 消费 `attrs.minBaseFee` 写入 [9,17)。

**Tech Stack:** C++20 协程（bcos::task）、boost::test、jsoncpp、Python（工具链与 e2e driver）、CMake/ctest。

**执行环境：** worktree `.claude/worktrees/op-alignment`（分支 `feat-opstack-e2e`，HEAD `b7d112b3f`）。工作区存在未提交改动（`opstack-executor/OpstackExecutor.h/.cpp`、t8n golden 若干）——**不要触碰、不要 add 它们**，每个任务只 add 自己改的文件。构建目录 `build/` 已存在（增量编译）。

**范围外（独立后续计划，本计划不涉及）：** BL-1 reorg/consolidation 能力（S-DRV-6/7，需 blockHash→block 映射架构设计）；BL-2 eth_getProof 历史块（Ledger MPT 持久化）；BL-4 Karst（NUT 机制 + EVMC_OSAKA 绑定，行业无参照实现）；MN-1（无 txpool，架构决策）；MN-2（与 op-geth 行为一致，无需改动）；MN-5（FCU V3/V4 不对称，需 op-node 版本验证，Task 2 会同步更新驱动）；MN-9（创世固定 feature flag，文档已注明）。

---

## 文件结构

| 文件 | 动作 | 责任 |
|---|---|---|
| `bcos-framework/bcos-framework/engine/Errors.h` | 修改 | 收纳全部 engine 异常类型（现有 `OpExecutionInternalError` + 从 EngineServiceImpl.h 移入 9 个） |
| `engine/bcos-engine/EngineServiceImpl.h` | 修改 | include Errors.h、删除本地异常声明（:71-98）；`detail` 区声明 `validateOpPayloadAttributes`；`updateForkchoice` OP 校验接线；`buildOpPayload` extraData lambda 写 minBaseFee；删除过期注释（:1143-1152） |
| `engine/bcos-engine/EngineServiceImpl.cpp` | 修改 | 实现 `validateOpPayloadAttributes`；`validatePayloadAttributes` 的 V4 parentBeaconBlockRoot 规则（`version == 3` → `version >= 3`） |
| `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h` | 新建 | inline `mapEngineErrorCode(bcos::Exception const&) noexcept` |
| `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp` | 修改 | 三个 handler 包 try/catch → 抛 `JsonRpcException`；删除 TODO（:112-116、:198-200） |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp` | 修改 | 输出 `requestsHash`（MN-3） |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp` | 修改 | deposit tx 输出 `depositReceiptVersion`（MN-6） |
| `tools/op-e2e/a1_active.py` | 修改 | FCU attrs 补全（gasLimit/eip1559Params/withdrawals/parentBeaconBlockRoot/minBaseFee）；getPayload 断言（含 minBaseFee extraData 尾部） |
| `tools/opstack-genesis/gen_eth_header_fixture.py` | 修改 | `--allocs` 时计算 MessagePasser 存储根作 withdrawalsRoot（MN-4） |
| `tools/opstack-genesis/chain-config.yaml` | 修改 | feature_flags 注释修正（MN-8） |
| `bcos-rpc/test/unittests/rpc/EngineErrorMapperTest.cpp` | 新建 | MJ-1 映射单元测试 |
| `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` | 修改 | mock 加 thrower、端点级错误码测试 |
| `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp` | 修改 | MN-3/MN-6 断言 |
| `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp` | 修改 | MJ-2 三个 Invalid 用例 + 修复 `ForkchoiceAttributesVersionGate`（version=3→2） |
| `tools/opstack-genesis/test_gen_eth_header_fixture.py` | 新建 | MN-4 工具测试 |

---

## Task 1: MJ-1 — engine 异常 → execution-apis JSON-RPC 错误码映射

**Files:**
- Modify: `bcos-framework/bcos-framework/engine/Errors.h`
- Modify: `engine/bcos-engine/EngineServiceImpl.h:71-98`
- Create: `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h`
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp`
- Create: `bcos-rpc/test/unittests/rpc/EngineErrorMapperTest.cpp`
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`

现状：`EngineServiceImpl` 抛的 `UnsupportedFork`/`UnknownPayload`/`InvalidForkchoiceState`/`UnsupportedOpPayloadAttributes` 都是 `bcos::Exception` 派生（`DERIVE_BCOS_EXCEPTION`），在 `Web3JsonRpcImpl::handleRequest`（bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.cpp:91-103）落进 `catch (...)` → 一律 `-32603`。op-node 无法区分 fork 不匹配与内部错误。映射放在端点层（EngineEndpoint），保持 Web3JsonRpcImpl 通用。

- [ ] **Step 1: 扩展 `bcos-framework/bcos-framework/engine/Errors.h`**

在 `namespace bcos::engine` 内、`OpExecutionInternalError` 声明之后追加（从 `EngineServiceImpl.h:71-98` 移入，含原注释）：

```cpp
// ---- Engine-API exceptions ----
// Live in bcos-framework (not bcos-engine) so bcos-rpc can name them for the JSON-RPC error-code
// mapping (EngineErrorMapper.h) without depending on the engine library.
DERIVE_BCOS_EXCEPTION(UnsupportedEngineApiVersion);
DERIVE_BCOS_EXCEPTION(GlobalStateStorageNotConfigured);
DERIVE_BCOS_EXCEPTION(UnknownForkchoiceHeadBlock);
DERIVE_BCOS_EXCEPTION(InvalidForkchoiceState);
DERIVE_BCOS_EXCEPTION(UnknownPayload);
DERIVE_BCOS_EXCEPTION(IncompatiblePayloadVersion);

/// JSON-RPC -38005 "Unsupported fork": the payload timestamp's fork and the called method version
/// disagree -- Isthmus+ payloads may only arrive on V4, pre-Isthmus timestamps may not use V4.
DERIVE_BCOS_EXCEPTION(UnsupportedFork);

/// JSON-RPC -38003 "Invalid payload attributes": an OP-mode forkchoiceUpdated carried payload
/// attributes the engine refuses.
DERIVE_BCOS_EXCEPTION(UnsupportedOpPayloadAttributes);

DERIVE_BCOS_EXCEPTION(OpPayloadBuildingUnsupported);
```

- [ ] **Step 2: `EngineServiceImpl.h` 改为 include Errors.h 并删除本地声明**

删除 `EngineServiceImpl.h:71-98` 的整块 `DERIVE_BCOS_EXCEPTION(...)` 声明与注释（`UnsupportedEngineApiVersion` 到 `OpPayloadBuildingUnsupported`，含 `// ---- OP-mode exceptions ----` 注释块），在现有 include 区（约 :40 附近）加：

```cpp
#include "bcos-framework/engine/Errors.h"
```

先确认 `OpPayloadBuildingUnsupported` 是否还有引用（Tier-2 构建上线后可能已无）：

```bash
grep -rn "OpPayloadBuildingUnsupported" engine/ opstack-executor/ bcos-rpc/ bcos-framework/ --include="*.h" --include="*.cpp"
```

预期：除 Errors.h 与 EngineServiceImpl.h 外无引用 → 保留声明即可（已无引用也留着，避免连锁改动）；若有引用，保持现状。

- [ ] **Step 3: 写失败测试 `bcos-rpc/test/unittests/rpc/EngineErrorMapperTest.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  ...
 */

#include <bcos-framework/engine/Errors.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(EngineErrorMapperTest)

BOOST_AUTO_TEST_CASE(mapsEngineExceptionTypesToExecutionApiCodes)
{
    // execution-apis codes (exec-engine.md references execution-apis error table).
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::UnsupportedFork{}), EngineError::UnsupportedFork);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::IncompatiblePayloadVersion{}), EngineError::UnsupportedFork);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::UnknownPayload{}), EngineError::UnknownPayload);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::InvalidForkchoiceState{}), EngineError::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::UnsupportedOpPayloadAttributes{}), EngineError::InvalidPayloadAttributes);
    // Everything not enumerated stays the generic internal error.
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::OpExecutionInternalError{}), InternalError);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::UnsupportedEngineApiVersion{}), InternalError);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::Error{BCOS_ERROR(1, "x")}), InternalError);
}

BOOST_AUTO_TEST_SUITE_END()
```

（`mapEngineErrorCode` 不存在 → 编译失败，即 TDD 的失败态。）

- [ ] **Step 4: 运行确认失败**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment
cmake --build build --target test-bcos-rpc -j8 2>&1 | tail -5
```

预期：编译错误 `'mapEngineErrorCode' was not declared`（EngineErrorMapper.h 不存在）。

- [ ] **Step 5: 新建 `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h`**

```cpp
#pragma once

#include "bcos-rpc/web3jsonrpc/utils/Common.h"  // EngineError
#include <bcos-framework/engine/Errors.h>
#include <bcos-utilities/Exceptions.h>

namespace bcos::rpc
{
/// Map engine-service exceptions to the execution-apis JSON-RPC error codes the Engine API
/// assigns (specs.optimism.io exec-engine.md references the execution-apis error table).
/// Unmapped conditions stay -32603 InternalError: a mapped code must be unambiguous.
/// `bcos::Error` (storage/service faults) intentionally maps to InternalError.
inline int32_t mapEngineErrorCode(bcos::Exception const& e) noexcept
{
    if (dynamic_cast<bcos::engine::UnsupportedFork const*>(&e) ||
        dynamic_cast<bcos::engine::IncompatiblePayloadVersion const*>(&e))
    {
        return EngineError::UnsupportedFork;  // -38005
    }
    if (dynamic_cast<bcos::engine::UnknownPayload const*>(&e))
    {
        return EngineError::UnknownPayload;  // -38001
    }
    if (dynamic_cast<bcos::engine::InvalidForkchoiceState const*>(&e))
    {
        return EngineError::InvalidForkchoiceState;  // -38002
    }
    if (dynamic_cast<bcos::engine::UnsupportedOpPayloadAttributes const*>(&e))
    {
        return EngineError::InvalidPayloadAttributes;  // -38003
    }
    return InternalError;  // -32603
}
}  // namespace bcos::rpc
```

- [ ] **Step 6: 编译运行，测试通过**

```bash
cmake --build build --target test-bcos-rpc -j8 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineErrorMapperTest
```

预期：2 个用例 PASS。

- [ ] **Step 7: `EngineEndpoint.cpp` 三 handler 转换 + 删死代码**

在 `EngineEndpoint.cpp` 头部 include 区（:25 后）加：

```cpp
#include <bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h>
```

在 `using namespace bcos::rpc;` 后（:29 后）加匿名命名空间：

```cpp
namespace
{
/// Convert an engine-service exception into the JSON-RPC error the Engine API assigns to the
/// condition. bcos::Exception-derived engine errors previously escaped to Web3JsonRpcImpl's
/// catch(...) and came back as -32603; converting at the endpoint keeps Web3JsonRpcImpl generic.
[[noreturn]] void rethrowAsJsonRpcError(bcos::Exception const& e)
{
    throw JsonRpcException(mapEngineErrorCode(e), e.what());
}
}  // namespace
```

`handleForkchoiceUpdated`（:100-121）——把引擎调用包进 try/catch，并删除 TODO 注释（:112-116）：

```cpp
    auto forkchoiceState = parseForkchoiceState(request);
    auto payloadAttrs = parsePayloadAttributes(request, version);
    bcos::engine::ForkchoiceUpdatedResult engineResult;
    try
    {
        engineResult = co_await engineService->updateForkchoice(forkchoiceState,
            payloadAttrs.has_value() ? &*payloadAttrs : nullptr, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    auto jsonResult = combineForkchoiceUpdatedResult(engineResult, version);
    buildJsonContent(jsonResult, response);
```

`handleGetPayload`（:143-165）——包 try/catch，保留 null 守卫（防御性）：

```cpp
    engine::PayloadID payloadId = request[0u].asString();
    bcos::engine::GetPayloadResult engineResult;
    try
    {
        engineResult =
            co_await engineService->getPayload(payloadId, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    if (!engineResult)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }
```

`handleNewPayload`（:187-205）——包 try/catch，删除 TODO 注释（:198-200）：

```cpp
    auto newPayloadReq = parseNewPayloadRequest(request, version);
    bcos::engine::PayloadStatus engineResult;
    try
    {
        engineResult =
            co_await engineService->newPayload(newPayloadReq, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    auto result = serializePayloadStatus(engineResult, version);
    buildJsonContent(result, response);
```

- [ ] **Step 8: 端点级测试——`EngineRpcTest.cpp` 的 mock 加 thrower 并新增用例**

`MockOpEngineService` 的 `State` 结构（EngineRpcTest.cpp:40-65 附近）追加成员：

```cpp
        std::function<void()> updateForkchoiceThrower;
        std::function<void()> getPayloadThrower;
        std::function<void()> newPayloadThrower;
```

三个 mock 方法体开头（`updateForkchoice` 的 `m_state->capturedForkchoiceState = ...` 之前等）各插入：

```cpp
        if (m_state->updateForkchoiceThrower)
        {
            m_state->updateForkchoiceThrower();
        }
```

（getPayload/newPayload 同理。）

新增用例（放在 `forkchoiceUpdatedV4` 用例之后）：

```cpp
// MJ-1: engine exceptions surface as the execution-apis codes, not -32603.
BOOST_AUTO_TEST_CASE(forkchoiceUpdatedUnsupportedForkMapsTo38005)
{
    mockService.m_state->updateForkchoiceThrower = [] {
        BOOST_THROW_EXCEPTION(bcos::engine::UnsupportedFork{}
                              << bcos::errinfo_comment{"fork mismatch"});
    };
    Json::Value params(Json::arrayValue);
    Json::Value fc;
    fc["headBlockHash"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    fc["safeBlockHash"] = "0x2222222222222222222222222222222222222222222222222222222222222222";
    fc["finalizedBlockHash"] = "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(fc);
    Json::Value response;
    bool threw = false;
    try
    {
        CALL_ENGINE(forkchoiceUpdatedV3, params, response);
    }
    catch (JsonRpcException const& e)
    {
        threw = true;
        BOOST_CHECK_EQUAL(e.code(), EngineError::UnsupportedFork);
    }
    BOOST_CHECK(threw);
}

BOOST_AUTO_TEST_CASE(getPayloadUnknownPayloadMapsTo38001)
{
    mockService.m_state->getPayloadThrower = [] {
        BOOST_THROW_EXCEPTION(bcos::engine::UnknownPayload{} << bcos::errinfo_comment{"no such id"});
    };
    Json::Value params(Json::arrayValue);
    params.append("0x0000000000000000");
    Json::Value response;
    bool threw = false;
    try
    {
        CALL_ENGINE(getPayloadV3, params, response);
    }
    catch (JsonRpcException const& e)
    {
        threw = true;
        BOOST_CHECK_EQUAL(e.code(), EngineError::UnknownPayload);
    }
    BOOST_CHECK(threw);
}
```

- [ ] **Step 9: 编译运行全部相关测试**

```bash
cmake --build build --target test-bcos-rpc -j8 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest,EngineErrorMapperTest
```

预期：EngineRpcTest 全部 PASS（含新增 2 个）、EngineErrorMapperTest 2 个 PASS。若原有 EngineRpcTest 有断言 `-32603` 的用例失败（grep `32603` 确认过没有），把该用例改断言为映射后的码。

- [ ] **Step 10: Commit**

```bash
git add bcos-framework/bcos-framework/engine/Errors.h engine/bcos-engine/EngineServiceImpl.h \
        bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h \
        bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp \
        bcos-rpc/test/unittests/rpc/EngineErrorMapperTest.cpp bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "fix(engine): map engine exceptions to execution-apis JSON-RPC codes (-38001/-38002/-38003/-38005)"
```

---

## Task 2: MJ-2 — OP 模式 FCU attrs 深度校验（gasLimit/eip1559Params/minBaseFee/withdrawals/beaconRoot）

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`（detail 声明 + updateForkchoice 接线）
- Modify: `engine/bcos-engine/EngineServiceImpl.cpp`（validateOpPayloadAttributes 实现 + validatePayloadAttributes V4 规则）
- Modify: `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp`（3 个新用例 + 修复 ForkchoiceAttributesVersionGate）
- Modify: `tools/op-e2e/a1_active.py`（驱动 attrs 补全——与 Task 3 同函数，一起改）

现状：OP 模式跳过 `validatePayloadAttributes`（EngineServiceImpl.h:270-283 `if constexpr (!c_opMode)`），缺 gasLimit 回退 ledgerConfig、eip1559Params 缺失用中性 1/1、withdrawals 非空/缺 parentBeaconBlockRoot 被静默规整——op-geth 在 `checkOptimismPayloadAttributes`（eth/catalyst/api_optimism.go:40-65）即拒。目标：与 op-geth 相同——非法 attrs 返回 STATUS_INVALID（-38003 语义），不更新 forkchoice 状态，不构建。

- [ ] **Step 1: 写失败测试——`OpNewPayloadRpcE2eTest.cpp` 的 `OpForkchoiceRpcE2eSuite` 新增 3 个用例**

在 `ForkchoiceAttributesVersionGate` 用例前插入（套件内 helper `runVectorAndGetBlockHash`/`forkFlagsFor` 已存在）：

```cpp
// MJ-2: OP-mode FCU attrs deep validation (op-geth checkOptimismPayloadAttributes,
// eth/catalyst/api_optimism.go:40-65). Invalid attrs return STATUS_INVALID *before* any
// forkchoice state change or build -- never a silent fallback.
namespace
{
bcos::engine::PayloadAttributes makeJovianAttrs()
{
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = 2'000'000'000'000;  // strictly after the golden parent (ms domain)
    attrs.prevRandao = bcos::crypto::HashType{};
    attrs.suggestedFeeRecipient = bcos::Address{};
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes{0, 0, 0, 8, 0, 0, 0, 2};  // denominator=8, elasticity=2
    attrs.minBaseFee = 0;
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::crypto::HashType{};
    return attrs;
}
}  // namespace

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMissingGasLimitInvalid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.gasLimit = std::nullopt;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMissingMinBaseFeeInvalid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.minBaseFee = std::nullopt;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("minBaseFee") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMinBaseFeeBeforeJovianInvalid)
{
    // Isthmus fixture: minBaseFee must be null pre-Jovian (jovian/exec-engine.md:59-79).
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(/*jovian=*/false));
    bcos::h256 genesisHash = fixture->genesisBlockHash();  // 若不存在该 helper，见下方注
    auto attrs = makeJovianAttrs();
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{genesisHash, genesisHash, genesisHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("minBaseFee") != std::string::npos);
}
```

注：`OpE2eFixture` 若没有 `genesisBlockHash()` helper，先读 `OpNewPayloadRpcE2eTest.cpp` 的 fixture 定义（搜 `struct OpE2eFixture`），用其暴露的 genesis hash 字段名替换（`ForkchoiceHeadUnknownSyncing` 用例用 `forkFlagsFor(false)` 构造 fixture 的方式可参考）。

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target opstack-executor-tests -j8 2>&1 | tail -3 && \
./build/opstack-executor/tests/opstack-executor-tests --run_test=OpForkchoiceRpcE2eSuite
```

预期：新用例失败——现状 `updateForkchoice` 对 V4 attrs 直接进 `buildOpPayload`（缺 gasLimit 回退 ledgerConfig 或抛 OpExecutionInternalError），测试期望 Invalid 状态。

- [ ] **Step 3: 实现 `detail::validateOpPayloadAttributes`（EngineServiceImpl.cpp）**

在 `EngineServiceImpl.cpp` 的 `validatePayloadAttributes` 实现（:190-230）之后追加：

```cpp
std::optional<std::string> bcos::engine::detail::validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive)
{
    // Rollup-mode FCU attrs validation (op-geth checkOptimismPayloadAttributes,
    // eth/catalyst/api_optimism.go:40-65). The OP face is Isthmus+/Holocene+, so the
    // Holocene eip1559Params and (from Jovian) minBaseFee presence rules are unconditional.
    if (!payloadAttributes.gasLimit.has_value())
    {
        return std::string("gasLimit parameter is required (OP rollup)");
    }
    if (!payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is required on the OP path (Holocene+)");
    }
    if (payloadAttributes.eip1559Params->size() != 8)
    {
        return std::string("eip1559Params must be exactly 8 bytes");
    }
    if (jovianActive && !payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee is required after the Jovian fork");
    }
    if (!jovianActive && payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee must be null before the Jovian fork");
    }
    return std::nullopt;
}
```

- [ ] **Step 4: `validatePayloadAttributes` 的 V4 规则补丁（EngineServiceImpl.cpp:225-228）**

```cpp
    if (version >= 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3 and V4");
    }
```

- [ ] **Step 5: `EngineServiceImpl.h` 声明 + `updateForkchoice` 接线**

`detail` 命名空间（`validatePayloadAttributes` 声明 :114-115 之后）追加：

```cpp
    /// OP-mode rollup attrs validation: gasLimit/eip1559Params/minBaseFee presence rules
    /// (op-geth checkOptimismPayloadAttributes). `jovianActive` is feature-driven
    /// (feature_op_jovian, constant across blocks).
    std::optional<std::string> validateOpPayloadAttributes(
        const PayloadAttributes& payloadAttributes, bool jovianActive);
```

`updateForkchoice` 中 `if (payloadAttributes != nullptr)` 块（:261-284）整体替换为：

```cpp
        if (payloadAttributes != nullptr)
        {
            // Rollup mode validates the SAME attributes surface the generic path does, plus the
            // OP-only rules (gasLimit/eip1559Params/minBaseFee). A validation failure returns
            // STATUS_INVALID before any forkchoice state change -- the op-geth ordering
            // (checkOptimismPayloadAttributes runs ahead of the state update,
            // eth/catalyst/api.go:215-218). `if constexpr` keeps the generic path's codegen
            // unchanged.
            if (auto validationError =
                    detail::validatePayloadAttributes(*payloadAttributes, version);
                validationError.has_value())
            {
                ForkchoiceUpdatedResult result{
                    .payloadStatus = makeStatus(
                        PayloadValidationStatus::Invalid, std::nullopt, validationError),
                    .payloadId = std::nullopt,
                };
                co_return result;
            }
            if constexpr (c_opMode)
            {
                if (auto validationError = detail::validateOpPayloadAttributes(
                        *payloadAttributes, m_scheduler.get().isJovianActive());
                    validationError.has_value())
                {
                    ForkchoiceUpdatedResult result{
                        .payloadStatus = makeStatus(
                            PayloadValidationStatus::Invalid, std::nullopt, validationError),
                        .payloadId = std::nullopt,
                    };
                    co_return result;
                }
            }
        }
```

- [ ] **Step 6: 修复 `ForkchoiceAttributesVersionGate`（钉死真实版本门）**

该用例当前传 `version=3` 却期望 `UnsupportedFork`——代码的版本门是 `version < 3`（EngineServiceImpl.h:392，V3+ 带 attrs 是被接受的，op-node 即用 FCU V3 建块），此测试在现行代码下**实际失败**（P0 的"陈旧二进制"判断不准确）。改为传 `version=2` 并更新注释：

```cpp
// ③ V1/V2 attrs -> UnsupportedFork (-38005): the OP build path gates attrs-carrying FCU at
// V3+ (op-node sends FCU V3 with attrs for Isthmus+ builds); V1/V2 attrs are refused at the
// version gate before any validation. Attrs deep validation (MJ-2) is exercised by the
// ForkchoiceAttrs*Invalid cases above.
BOOST_AUTO_TEST_CASE(ForkchoiceAttributesVersionGate)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = 2'000'000'000'000;
    attrs.prevRandao = bcos::crypto::HashType{};
    attrs.suggestedFeeRecipient = bcos::Address{};
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
                          /*version=*/2)),
        bcos::engine::UnsupportedFork);
}
```

- [ ] **Step 7: 运行 opstack 测试套件**

```bash
cmake --build build --target opstack-executor-tests opstack-executor-block-tests -j8 2>&1 | tail -3
./build/opstack-executor/tests/opstack-executor-tests --run_test=OpForkchoiceRpcE2eSuite
./build/opstack-executor/tests/opstack-executor-block-tests --run_test=OpForkchoiceRpcE2eSuite
```

预期：新增 3 用例 PASS；`ForkchoiceAttributesVersionGate` PASS（block-tests 重新编译后不再失败）。

- [ ] **Step 8: 更新 `tools/op-e2e/a1_active.py`（驱动 attrs 补全 + getPayload 断言）**

`a1_active.py` 第 4/5 步（:124-142 附近）替换为：

```python
    # 4. FCU with attrs -> Tier-2 attribute-driven building: VALID + payloadId. The attrs mirror
    # op-node's rollup shape (op-service/eth/types.go PayloadAttributes): gasLimit from the head
    # (SystemConfig-derived), Holocene eip1559Params (denominator=8, elasticity=2), empty
    # withdrawals, parentBeaconBlockRoot, and (Jovian chains) minBaseFee. The engine validates
    # this surface (op-geth checkOptimismPayloadAttributes) and refuses to build on deviation.
    now = int(time.time())
    head_gas = int(head_block["gasLimit"], 16)
    genesis_extra = eth.call("eth_getBlockByNumber", ["0x0", False])["extraData"]
    jovian = len(bytes.fromhex(genesis_extra[2:])) == 17  # Jovian 17B extraData shape
    attrs = {"timestamp": hex(now), "prevRandao": "0x" + "00" * 32,
             "suggestedFeeRecipient": "0x4200000000000000000000000000000000000011",
             "gasLimit": hex(head_gas),
             "eip1559Params": "0x0000000800000002",
             "withdrawals": [],
             "parentBeaconBlockRoot": "0x" + "00" * 32}
    if jovian:
        attrs["minBaseFee"] = "0x0"
    fc2 = eng.call(f"engine_forkchoiceUpdatedV{ver}", [fcs, attrs])
    check("FCU attrs builds (VALID + payloadId, Tier-2)",
          fc2["payloadStatus"]["status"] == "VALID" and fc2.get("payloadId") is not None,
          str(fc2)[:120])

    # 5. getPayload serves the built payload (Tier-2). Jovian chains: the extraData tail
    # [9,17) must echo the requested minBaseFee (audit BL-3).
    pl = eng.call(f"engine_getPayloadV{ver}", [fc2["payloadId"]])
    pl_head = pl["executionPayload"]
    check("getPayload serves the built payload (Tier-2)",
          int(pl_head["blockNumber"], 16) == int(head_block["number"], 16) + 1,
          str(pl)[:120])
    if jovian:
        extra = bytes.fromhex(pl_head["extraData"][2:])
        check("Jovian extraData minBaseFee tail == 0 (BL-3)",
              len(extra) == 17 and int.from_bytes(extra[9:17], "big") == 0, str(extra))
```

（若 `ver` 在驱动中不是 4，先确认 `getPayloadV{ver}` 与 build 版本兼容——`isGetPayloadVersionCompatible` 允许 `requestVersion >= payloadVersion`，V3/V4 均可取回。）

- [ ] **Step 9: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h engine/bcos-engine/EngineServiceImpl.cpp \
        opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp tools/op-e2e/a1_active.py
git commit -m "fix(engine): OP FCU attrs deep validation (gasLimit/eip1559Params/minBaseFee) — STATUS_INVALID instead of silent fallback"
```

---

## Task 3: BL-3 — Jovian minBaseFee 构建侧写入 extraData

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`（buildOpPayload extraData lambda，:747-751）
- Modify: `tools/op-e2e/a1_active.py`（Task 2 Step 8 已含断言）

现状：Jovian 分支 `extra[0]=0x01; extra.resize(17,0x00)` 尾 8 字节恒零；`attrs.minBaseFee` 仅被解析（EngineHelper.cpp:358-369）与入 payloadId（PayloadId.h:222-232），从未写入构建块的 extraData。后果：SystemConfig 下发 minBaseFee>0 时构建块与 op-geth（`EncodeJovianExtraData`，consensus/misc/eip1559/eip1559_optimism.go:49-54,180-190）不一致 → 跨客户端 blockHash 判 INVALID。读取侧（calcOpBaseFee 的 floor，EngineServiceImpl.cpp:314-327,370-374）已就绪，只缺写入。

- [ ] **Step 1: 确认失败测试已就位（Task 2 Step 8 的断言）**

`a1_active.py` 的 "Jovian extraData minBaseFee tail == 0 (BL-3)" 断言在 C2 链（feature_op_jovian + 17B 创世 extraData）上运行即验证。当前代码尾 8 字节恒零 → 断言通过（minBaseFee 请求 0x0）；把驱动断言改为请求**非零** minBaseFee 才能测出写入：在 `a1_active.py` Step 8 中把 `attrs["minBaseFee"] = "0x0"` 改为 `attrs["minBaseFee"] = "0x3b9aca00"`，断言改为：

```python
        check("Jovian extraData minBaseFee tail (BL-3)",
              len(extra) == 17 and int.from_bytes(extra[9:17], "big") == 1000000000,
              str(extra))
```

在未修代码上运行 → 断言失败（tail 恒 0）。

- [ ] **Step 2: 实现 extraData lambda（EngineServiceImpl.h:747-751）**

```cpp
                    if (m_scheduler.get().isJovianActive())
                    {
                        extra[0] = 0x01;
                        extra.resize(17, 0x00);
                        // Jovian: minBaseFee u64 BE at [9,17) (op-geth
                        // EncodeJovianExtraData, eip1559_optimism.go:49-54). The attrs field is
                        // REQUIRED after Jovian (updateForkchoice validation); the absent
                        // fallback (0 = the spec default) only serves direct-service callers.
                        if (auto minBaseFee = payloadAttributes.minBaseFee;
                            minBaseFee.has_value())
                        {
                            for (std::size_t i = 0; i < 8; ++i)
                            {
                                extra[9 + i] = static_cast<bcos::byte>(
                                    (*minBaseFee >> (56 - 8 * i)) & 0xFF);
                            }
                        }
                    }
```

- [ ] **Step 3: 编译 + 单元回归**

```bash
cmake --build build --target bcos-evm-opstack-tests opstack-executor-tests opstack-executor-block-tests test-bcos-rpc -j8 2>&1 | tail -3
./build/bcos-evm/test/bcos-evm-opstack-tests
./build/opstack-executor/tests/opstack-executor-tests
./build/opstack-executor/tests/opstack-executor-block-tests
```

预期：全绿（构建面改动不应影响执行语义测试；若 `extraData` 相关 golden 测试失败，检查是否 mock 了 minBaseFee 后行为变化——不应有，golden 向量不走 buildOpPayload）。

- [ ] **Step 4: C2 链 e2e 验证（需要 devnet 环境）**

```bash
bash /tmp/c2/start_op_node.sh   # 或 tools/op-e2e/start_c2_op_node.sh（handoff 文档）
python3 tools/op-e2e/a1_active.py
```

预期："Jovian extraData minBaseFee tail (BL-3)" PASS。若本机无运行中的 C2 链，此项在 CI/后续验证，注明即可（单元测试是主门禁）。

- [ ] **Step 5: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h tools/op-e2e/a1_active.py
git commit -m "fix(engine): write attrs.minBaseFee into Jovian extraData [9,17) on build (audit BL-3/S-BLD-4)"
```

---

## Task 4: MN-3 — block JSON 输出 requestsHash

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp:136-143`
- Modify: `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp`（combineBlockResponseGasLimitAndParentBeaconBlockRoot）

现状：op-geth block JSON 输出 `requestsHash`（internal/ethapi/api.go:1092-1094，仅非 nil 时）；FISCO 不输出。共识值正确（Isthmus+ 恒 sha256('')，`c_opEmptyRequestsHash`），仅 RPC 输出不一致。

- [ ] **Step 1: 写失败测试——`Web3ResponseTest.cpp` 的 `combineBlockResponseGasLimitAndParentBeaconBlockRoot`**

第一个块（unset 头）断言区（:95-98 附近）追加：

```cpp
        BOOST_CHECK(!result.isMember("requestsHash"));  // PBFT/unset -> absent (op-geth omitempty)
```

第二个块（set 头）断言区（:118-120 附近）追加 set 与断言：

```cpp
        header->setRequestsHash(crypto::HashType(
            "0x2222222222222222222222222222222222222222222222222222222222222222"));
```

```cpp
        BOOST_CHECK_EQUAL(result["requestsHash"].asString(),
            "0x2222222222222222222222222222222222222222222222222222222222222222");
```

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target test-bcos-rpc -j8 && \
./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest/combineBlockResponseGasLimitAndParentBeaconBlockRoot
```

预期：`!result.isMember("requestsHash")` 失败（当前恒不输出）。

- [ ] **Step 3: 实现（BlockResponse.cpp:136-143 parentBeaconBlockRoot 块之后）**

```cpp
    // EIP-7685 requestsHash: Isthmus+ OP headers carry sha256('') (set by the seal and rebuilt
    // by the engine); PBFT/pre-Isthmus headers have no value (nullopt) and keep the field
    // absent -- symmetric with op-geth (internal/ethapi/api.go:1092-1094, omitempty).
    if (auto requestsHash = blockHeader->requestsHash(); requestsHash.has_value())
    {
        result["requestsHash"] = requestsHash->hexPrefixed();
    }
```

- [ ] **Step 4: 运行通过 + 可选 e2e 断言**

```bash
./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest
```

预期：PASS。可选：`tools/op-e2e/rpc_matrix.py` 的块断言处（:196-207 附近）追加 `check("block requestsHash = sha256('')", block["requestsHash"] == "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", ...)`。

- [ ] **Step 5: Commit**

```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp
git commit -m "feat(rpc): output requestsHash in block JSON (audit MN-3/S-RPC-12)"
```

---

## Task 5: MN-6 — deposit tx JSON 输出 depositReceiptVersion

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp:24-31`
- Modify: `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp`（combineTxResponseDepositNonceFromReceiptMeta）

现状：回执已输出 depositReceiptVersion，但 deposit tx 对象不输出（op-geth internal/ethapi/api.go:1210-1213 在 tx 响应也输出）。

- [ ] **Step 1: 写失败测试——`combineTxResponseDepositNonceFromReceiptMeta`**

有 meta 分支（:223-225 附近，nonce 断言之后）追加：

```cpp
    BOOST_CHECK_EQUAL(result["depositReceiptVersion"].asString(), "0x1");
```

无 meta 分支（:228-230 附近）追加：

```cpp
    BOOST_CHECK(!result2.isMember("depositReceiptVersion"));
```

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target test-bcos-rpc -j8 && \
./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest/combineTxResponseDepositNonceFromReceiptMeta
```

预期：`result["depositReceiptVersion"]` 断言失败（字段不存在）。

- [ ] **Step 3: 实现（TransactionResponse.cpp:24-31）**

```cpp
    if (auto extraBytes = tx.extraTransactionBytes();
        !extraBytes.empty() && extraBytes[0] == c_depositTxType)
    {
        if (auto meta = receipt.opStackMeta(); meta.has_value() && meta->deposit_nonce)
        {
            result["nonce"] = toQuantity(*meta->deposit_nonce);
            // op-geth also surfaces depositReceiptVersion on the tx object
            // (internal/ethapi/api.go:1210-1213); absent without the meta.
            if (meta->deposit_receipt_version)
            {
                result["depositReceiptVersion"] = toQuantity(*meta->deposit_receipt_version);
            }
        }
    }
```

- [ ] **Step 4: 运行通过**

```bash
./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest
```

预期：PASS。

- [ ] **Step 5: Commit**

```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp
git commit -m "feat(rpc): output depositReceiptVersion on deposit tx JSON (audit MN-6)"
```

---

## Task 6: MN-7 — 删除 EngineServiceImpl.h 过期注释

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h:1143-1152`

- [ ] **Step 1: 替换注释块**

`handleOpNewPayload` 中 V4-only 门后面的 "NOTE (independent review #5429, finding B)" 注释块（:1143-1152）整体替换为：

```cpp
        // NOTE (audit v2, 2026-08-21): the historical "#5429 finding B" note claiming this
        // V4-only gate is unreachable through the production composition root is obsolete --
        // the OP root passes maxEngineVersion=4 (libinitializer/Initializer.cpp:620), the V4
        // endpoints are registered (EndpointsMapping.cpp:63-71) and supportedOpCapabilities
        // advertises the V4 trio (EngineServiceImpl.cpp:129-141).
```

- [ ] **Step 2: 编译确认**

```bash
cmake --build build --target opstack-executor-tests -j8 2>&1 | tail -3
```

预期：编译通过（纯注释改动）。

- [ ] **Step 3: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h
git commit -m "docs(engine): drop obsolete #5429 finding-B comment (V4 is wired, audit MN-7)"
```

---

## Task 7: MN-8 — chain-config.yaml feature_flags 注释修正

**Files:**
- Modify: `tools/opstack-genesis/chain-config.yaml:9-11`

- [ ] **Step 1: 先确认哪些配置文件的注释是陈旧的**

```bash
grep -rn "injected by the" tools/opstack-genesis/*.yaml
```

预期：`chain-config.yaml` 命中（模板 chain-config.template.yaml 已是"VERIFIES"新文案）。若 `chain-config-c2.yaml` 也命中，一并修。

- [ ] **Step 2: 替换注释**

`chain-config.yaml:9-11`：

```
# SystemConfig storage is NOT seeded here — `feature_flags` is injected by the
# C++ genesis path (Ledger), which alone knows the version-gated feature set;
# the remaining SystemConfig config entries (owner, chain_id, gas/version) are a
```

替换为：

```
# SystemConfig storage is NOT seeded here — the node VERIFIES `feature_flags` at
# first start against Features::toFlagsNumber() (Ledger) and refuses to build
# genesis on mismatch; the genesis state root commits the value on both FISCO
# and the op-reth oracle. The remaining SystemConfig config entries (owner,
# chain_id, gas/version) are a
```

（保留后续行原文。）

- [ ] **Step 3: Commit**

```bash
git add tools/opstack-genesis/chain-config.yaml
git commit -m "docs(genesis): fix stale feature_flags injection comment (audit MN-8)"
```

---

## Task 8: MN-4 — 创世工具计算 MessagePasser 存储根作 withdrawalsRoot

**Files:**
- Modify: `tools/opstack-genesis/gen_eth_header_fixture.py`
- Create: `tools/opstack-genesis/test_gen_eth_header_fixture.py`

现状：`gen_eth_header_fixture.py:69` 的 `withdrawals_root` 恒 `EMPTY_TRIE_ROOT`；Isthmus+ 创世规范要求 = L2ToL1MessagePasser（0x4200…0016）账户存储根（isthmus/exec-engine.md:100-101，op-geth core/genesis.go:711-719）。当前 Phase A 链 passer 无存储 → 空根正确；但切换到 op-deployer 代理布局（带存储）时工具不会自动跟随。

- [ ] **Step 1: 写失败测试 `tools/opstack-genesis/test_gen_eth_header_fixture.py`**

```python
#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Tests for gen_eth_header_fixture.py withdrawals_root computation (audit MN-4)."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

_HERE = pathlib.Path(__file__).parent
_SPEC = importlib.util.spec_from_file_location(
    "gen_eth_header_fixture", str(_HERE / "gen_eth_header_fixture.py"))
_FIXTURE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_FIXTURE)

from mpt_state_root import compute_storage_root  # noqa: E402
from build_allocs import keccak256  # noqa: E402

PASSER = "0x4200000000000000000000000000000000000016"


class TestWithdrawalsRoot(unittest.TestCase):
    def test_allocs_without_passer_keeps_empty_root(self):
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write("[alloc.1]\naddress=0x1234567890123456789012345678901234567890\n")
            path = handle.name
        try:
            out = subprocess.run(
                [sys.executable, str(_HERE / "gen_eth_header_fixture.py"),
                 "--toml", "--allocs", path],
                capture_output=True, text=True, check=True).stdout
        finally:
            pathlib.Path(path).unlink()
        self.assertIn("withdrawals_root=" + _FIXTURE.EMPTY_TRIE_ROOT, out)

    def test_passer_storage_drives_withdrawals_root(self):
        # keccak256(rlp(keccak(slot))) trie over one slot, computed independently.
        slot, value = "0x" + "00" * 31 + "01", "0x" + "00" * 31 + "02"
        expected = compute_storage_root([(slot, value)])
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write(f"[alloc.1]\naddress={PASSER}\nbalance=0\n")
            handle.write(f"[alloc.1.storage]\n{slot}={value}\n")
            path = handle.name
        try:
            out = subprocess.run(
                [sys.executable, str(_HERE / "gen_eth_header_fixture.py"),
                 "--toml", "--allocs", path],
                capture_output=True, text=True, check=True).stdout
        finally:
            pathlib.Path(path).unlink()
        self.assertIn("withdrawals_root=0x" + expected.hex(), out)


if __name__ == "__main__":
    unittest.main()
```

注：先读 `mpt_state_root.py` 的 `parse_allocs_ini` 确认 INI 语法（`[alloc.N.storage]` 子节的确切 key 格式：slot 行是 `slot=value` 还是 `key=value`），按实际格式调整上面测试的 INI 内容。

- [ ] **Step 2: 运行确认失败**

```bash
cd tools/opstack-genesis && python3 -m unittest test_gen_eth_header_fixture -v
```

预期：`test_passer_storage_drives_withdrawals_root` 失败（输出恒 EMPTY_TRIE_ROOT）。

- [ ] **Step 3: 实现——`gen_eth_header_fixture.py`**

import 行（:40）改为：

```python
from mpt_state_root import parse_allocs_ini, compute_state_root, compute_storage_root
```

`main()` 的 `--allocs` 分支（:179-181）替换为：

```python
    if args.allocs:
        allocs = parse_allocs_ini(args.allocs)
        fields["state_root"] = "0x" + compute_state_root(allocs).hex()
        # Isthmus+ genesis: withdrawalsRoot = L2ToL1MessagePasser storage root
        # (isthmus/exec-engine.md:100-101; op-geth core/genesis.go:711-719). Phase A deploys
        # the passer with empty storage -> empty-trie root; a proxied op-deployer layout
        # carries storage and the tool must track it.
        for alloc in allocs:
            if alloc["address"].lower() == PASSER_ADDRESS:
                fields["withdrawals_root"] = "0x" + compute_storage_root(
                    alloc.get("storage", [])).hex()
                break
```

模块常量区（:45 附近）加：

```python
PASSER_ADDRESS = "0x4200000000000000000000000000000000000016"
```

- [ ] **Step 4: 运行通过 + 现网 artifact 不变性验证**

```bash
cd tools/opstack-genesis && python3 -m unittest test_gen_eth_header_fixture -v
# 当前 C2 链：passer 空存储 → 输出应不变（diff 为空）
python3 gen_eth_header_fixture.py --toml --allocs op-fork-base-allocs.json > /tmp/header_new.toml
# 与既有 chain-config-c2.yaml 的 [eth_genesis_header] 节对比（或 git diff 工作区无该文件则人工比对 hash 行）
grep "^hash=" /tmp/header_new.toml
```

预期：单测 PASS；hash 与链上既有创世一致（passer 无存储 → 空根不变）。

- [ ] **Step 5: Commit**

```bash
git add tools/opstack-genesis/gen_eth_header_fixture.py tools/opstack-genesis/test_gen_eth_header_fixture.py
git commit -m "fix(genesis): compute MessagePasser storage root as Isthmus genesis withdrawalsRoot (audit MN-4/S-GEN-3)"
```

---

## Task 9: 全量回归与收尾

- [ ] **Step 1: 编译全部受影响目标**

```bash
cmake --build build --target bcos-evm-opstack-tests opstack-executor-tests opstack-executor-block-tests test-bcos-rpc -j8 2>&1 | tail -5
```

预期：零错误零警告（新增警告需修）。

- [ ] **Step 2: 运行全部相关测试**

```bash
./build/bcos-evm/test/bcos-evm-opstack-tests
./build/opstack-executor/tests/opstack-executor-tests
./build/opstack-executor/tests/opstack-executor-block-tests
./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest,EngineErrorMapperTest,Web3ResponseTest,EngineProtoAlignB1Test
cd tools/opstack-genesis && python3 -m unittest test_build_allocs test_gen_eth_header_fixture -v
```

预期：全部 PASS。`OpstackExecutorBlockTests` 的 `ForkchoiceAttributesVersionGate` 必须 PASS（Task 2 Step 6 修复）。

- [ ] **Step 3: git status 确认只含计划内文件**

```bash
git status --short | grep -v "^??" | head -20
```

预期：只出现本计划 Task 1-8 提交的文件；`opstack-executor/OpstackExecutor.h`、`opstack-executor/tests/OpstackExecutorTest.cpp`、t8n golden 等既有未提交改动不在列（它们保持未提交状态）。

- [ ] **Step 4: 更新审计报告差距清单状态**

`docs/2026-08-21-opstack-el-spec-audit-v2.md` §5 差距清单：把 BL-3、MJ-1、MJ-2、MN-3、MN-4、MN-6、MN-7、MN-8 各项标注"已修复于 <commit>（本计划 Task N）"。提交：

```bash
git add docs/2026-08-21-opstack-el-spec-audit-v2.md
git commit -m "docs(audit): mark Engine/RPC surface gaps (BL-3/MJ-1/MJ-2/MN-3/4/6/7/8) as fixed"
```

- [ ] **Step 5: 验证 push 指引**

分支 tracking 陈旧（`.merge` 指向 `feat-op-block-scheduler-standalone`），如需推送用显式 refspec：

```bash
git push ywy2090 HEAD:feat-opstack-e2e
```

---

## 自审记录

- **Spec 覆盖**：BL-3→Task 3；MJ-1→Task 1；MJ-2→Task 2（含 S-SYC-12 的 minBaseFee null/非 null 语义）；MN-3→Task 4；MN-4→Task 8；MN-6→Task 5；MN-7→Task 6；MN-8→Task 7；MN-5 的驱动侧在 Task 2 Step 8 同步处理；MN-1/MN-2/MN-5(验证)/MN-9 在"范围外"列出理由。
- **已知测试修复**：`ForkchoiceAttributesVersionGate`（version=3→2）是现行代码下的真实失败用例（P0 的"陈旧二进制"判断不成立），Task 2 Step 6 修复；`a1_active.py` 第 5 步 "getPayload refused" 已过期（Tier-2 后 getPayload 可用），Task 2 Step 8 一并更新。
- **类型一致性**：`WithdrawalV1`（bcos-framework/engine/Types.h，buildOpPayload 已用）；`PayloadAttributes::minBaseFee`（Types.h:106，uint64）；`requestsHash()`（BlockHeader.h:184）；`deposit_receipt_version`（TransactionReceipt.h:49）；`compute_storage_root`（mpt_state_root.py）；`EngineError::*`（bcos-rpc utils/Common.h:33-42）；`InternalError`（bcos-rpc jsonrpc/Common.h:69）。
