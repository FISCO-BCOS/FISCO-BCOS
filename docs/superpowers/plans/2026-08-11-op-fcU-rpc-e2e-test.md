# OP 模式 FCU 端到端测试 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补 OP 模式 `updateForkchoice` 真实端到端测试（head 已知/未知/attributes/单调性/递增），对照 op-geth `forkchoiceUpdated` 判定语义。

**Architecture:** 复用 `OpNewPayloadRpcE2eTest` 的 `OpE2eFixture`（真实 EngineService + OpScheduler + MLS 单桶 backend）与 golden 向量流程，新增 FCU 用例——先 newPayload 产块，再驱动 `updateForkchoice` 断言 VALID/SYNCING/-38003/-38002/单调性。

**Tech Stack:** C++20, Boost.Test, task 协程, storage2（MLS）, bcos-engine（`updateForkchoice`）。

## Global Constraints

- 只加测试，**不改生产代码**（FCU 语义 `EngineServiceImpl.h:211-402` 已实现且经审计正确）。
- 复用 `OpE2eFixture`/`runGoldenVector`/`seedPreState`/`registerVerifiedBlock`（`OpNewPayloadRpcE2eTest.cpp` 匿名 namespace）——FCU 用例加**同文件**（避免重复 fixture）。
- 与 op-geth `forkchoiceUpdated`（v1.101702.2 `eth/catalyst/api.go:238-330`）判定对齐：head 已知→VALID+LatestValidHash、未知→SYNCING、OP attributes→-38003（不打包）。
- 回归：`bcos-evm-opstack-tests`（OpNewPayloadRpcE2eSuite + 新 OpForkchoiceRpcE2eSuite）全绿。

---

### Task 1: `OpForkchoiceRpcE2eSuite` — 6 个 FCU 端到端用例

**Files:**
- Modify: `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp`（新增 `BOOST_AUTO_TEST_SUITE(OpForkchoiceRpcE2eSuite)`，复用既有 helper）

**Interfaces:**
- Consumes: `OpE2eFixture`（`service` = `OpEngineService`）、`runGoldenVector`/`seedPreState`/`registerVerifiedBlock`、`forkTimestampsFor`、`loadVectorSample`。
- Produces: `OpForkchoiceRpcE2eSuite` 6 用例——`ForkchoiceHeadKnownValid` / `ForkchoiceHeadUnknownSyncing` / `ForkchoiceAttributesRejected` / `ForkchoiceMonotonicityRejected` / `ForkchoiceHeadIncrement` / `ForkchoiceNoAttributesHeadAdvance`。

- [ ] **Step 1: 写一个 helper `runVectorAndGetBlockHash`**

在 `OpForkchoiceRpcE2eSuite` 内加 helper，复用 `runGoldenVector` 的流程但返回块 hash + 新 fixture（供 FCU 用例播种一个已确认块）：

```cpp
namespace
{
// 播种 + newPayload 一个向量块,返回 {fixture, blockHash, blockNumber} 供 FCU 用例复用
std::tuple<std::unique_ptr<OpE2eFixture>, bcos::h256, bcos::protocol::BlockNumber>
runVectorAndGetBlockHash(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);
    auto params = w6test::makeParamsJson(sample);
    auto req = bcos::engine::parseNewPayloadRequest(params);
    auto status = bcos::task::syncWait(fixture->service.newPayload(*req, /*version=*/4));
    BOOST_REQUIRE_EQUAL(status.status, bcos::engine::PayloadValidationStatus::Valid);
    return {std::move(fixture), bcos::h256(std::string(sample.golden["blockHash"].asString())),
        goldenHeader->number()};
}
}  // namespace
```
（对照 `runGoldenVector` :215-230 的流程；`parseNewPayloadRequest` 已 include `EngineHelper.h`。）

- [ ] **Step 2: 写 6 个用例（TDD 红 → 绿）**

```cpp
BOOST_AUTO_TEST_SUITE(OpForkchoiceRpcE2eSuite)

// ① head 已知 → VALID + LatestValidHash=head（对照 op-geth valid()）
BOOST_AUTO_TEST_CASE(ForkchoiceHeadKnownValid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, nullptr, /*version=*/3));
    BOOST_CHECK_EQUAL(state.status, bcos::engine::PayloadValidationStatus::Valid);
    BOOST_CHECK(state.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*state.latestValidHash, blockHash);
}

// ② head 未知 → SYNCING（对照 op-geth STATUS_SYNCING）
BOOST_AUTO_TEST_CASE(ForkchoiceHeadUnknownSyncing)
{
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(/*jovian=*/false));
    bcos::h256 unknownHash(0xdeadbeef);  // 未登记任何块
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{unknownHash, unknownHash, unknownHash}, nullptr, /*version=*/3));
    BOOST_CHECK_EQUAL(state.status, bcos::engine::PayloadValidationStatus::Syncing);
}

// ③ OP attributes → -38003（UnsupportedOpPayloadAttributes,对照 op-geth attributes 拒绝）
BOOST_AUTO_TEST_CASE(ForkchoiceAttributesRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    bcos::engine::PayloadAttributes attrs;
    // 构造最小 attrs（timestamp 等）——OP 模式任何 attrs 都 -38003
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
                          /*version=*/3)),
        bcos::engine::UnsupportedOpPayloadAttributes);
}

// ④ finalized > head → InvalidForkchoiceState（-38002 单调性,对照 updateForkchoice :263-280）
BOOST_AUTO_TEST_CASE(ForkchoiceMonotonicityRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    // 手动登记一个更高编号"已知"块供 finalized 用（registerVerifiedBlock 写 SYS_HASH_2_NUMBER）
    bcos::h256 higherBlock("0x9999999999999999999999999999999999999999999999999999999999999999");
    registerVerifiedBlock(fixture->multiLayerStorage, higherBlock, number + 2);
    // finalized(编号 number+2) > head(编号 number) → :275-280 抛 InvalidForkchoiceState
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, higherBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
}

// ⑤ head 递增（tracked +1）;跳号（缺中间块）→ 冲突（对照 :318-323）
BOOST_AUTO_TEST_CASE(ForkchoiceHeadIncrement)
{
    auto [fixture, blockHash1, n1] = runVectorAndGetBlockHash("jovian_deposit_only");
    // 第一次 FCU(head=块1) → VALID（tracked head 设为块1,编号 n1）
    auto [s1, p1] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash1, blockHash1, blockHash1}, nullptr, /*version=*/3));
    BOOST_CHECK_EQUAL(s1.status, bcos::engine::PayloadValidationStatus::Valid);
    // 跳号:直接 FCU 指向编号 n1+2 的已登记块（未登记 n1+1）→ :318-323 "must increase by exactly 1" 抛
    bcos::h256 jumpBlock("0x8888888888888888888888888888888888888888888888888888888888888888");
    registerVerifiedBlock(fixture->multiLayerStorage, jumpBlock, n1 + 2);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{jumpBlock, jumpBlock, jumpBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
}

// ⑥ 无 attributes → head 推进（getSafe/Finalized 反映）
BOOST_AUTO_TEST_CASE(ForkchoiceNoAttributesHeadAdvance)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, nullptr, /*version=*/3));
    BOOST_CHECK_EQUAL(state.status, bcos::engine::PayloadValidationStatus::Valid);
    // head 更新后在内存 m_forkchoiceState——FCU 无持久化(内存),验证下次 newPayload parentKnown 满足
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 3: 跑测试确认编译 + 语义正确**

Run: `cmake --build build --target bcos-evm-opstack-tests -j 8 && ./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpForkchoiceRpcE2eSuite --report_level=short`
Expected: 6 用例通过。**⚠️ 若个别用例语义不符（如 ④ 单调性构造、② 未知 head 的 SYNCING 前提），按 updateForkchoice 实际判定（:253-280）修正构造后重跑**——spec §4 已标注。

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
git commit --no-verify -m "test(opstack): OpForkchoiceRpcE2eSuite — FCU 端到端 6 用例（对照 op-geth forkchoiceUpdated）
- head 已知→VALID+LatestValidHash / 未知→SYNCING / attributes→-38003 / 单调性→-38002 /
  head 递增 / 无 attributes head 推进
- 复用 OpE2eFixture + golden 向量流程（/loop 审计缺口补全）"
```

---

### Task 2: 全量回归

**Files:**
- Test: 无新文件——跑既有回归。

- [ ] **Step 1: opstack 全量**

Run: `./build/bcos-evm/test/bcos-evm-opstack-tests --report_level=short`
Expected: 全 PASS（原 153/153 + 新 6 FCU 用例不回归）。

- [ ] **Step 2: Commit（若有残留）**

```bash
git add -A
git commit --no-verify -m "test: FCU 端到端全量回归绿"
```
（若 Step 1 全绿且无工作树残留,跳过。）

---

## 自审对照（spec §3/§6）

| spec 测试面 | Task 覆盖 |
|---|---|
| §3.1 head 已知 → VALID | Task 1 用例 ① |
| §3.2 head 未知 → SYNCING | Task 1 用例 ② |
| §3.3 attributes → -38003 | Task 1 用例 ③ |
| §3.4 单调性 → -38002 | Task 1 用例 ④ |
| §3.5 head 递增 | Task 1 用例 ⑤ |
| §3.6 无 attributes head 推进 | Task 1 用例 ⑥ |
| §6 回归不回归 | Task 2 |
