# Plan B（夹具层）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `subagent-driven-development`（推荐）或 `executing-plans` 逐任务实施。步骤用 `- [ ]` 勾选跟踪。
> 设计依据：`docs/plans/2026-09-12-plan-B-design.md`（已获用户核准）；规格：`docs/plans/2026-09-12-plan-B-fixture-layer.md`（含 B3/B4/B5「补齐」节）。
> 本文与规格冲突时**以本文为准**（本文已按源码复核并收窄了两处）。

**Goal:** 让引擎夹具能表达"一条跨全部 9 个 fork 的连续链"，并补齐逐 fork 收据 meta 形状、F8 负向格、fork 调度跨重开持久化。

**Architecture:** ①夹具注入（`ImportServiceFixtureT` 增加 tag 构造 + 9 档 schedule 工厂）；②以"手搓请求 + newPayload + FCU"串链驱动 9 档（复用已证可用的 `seedCanonicalChainABC` 形态，而不是单块 `buildPayloadAt`）；③断言仍落在执行路径上（meta / 负向 / ledger 持久化）。

**Tech Stack:** C++20 / Boost.Test / evmone / CMake(Ninja)。工作树 `/Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal`（分支 `feat/karst-on-318-merged`）。

---

## 0. 环境与操作契约（每步都适用）

- **只改这一个工作树**；主仓脏工作区与 `karst-on-5550` 不得改动；**不推送**；`docs/**` 与 `.agents/**` 永不入库；**禁止目录级 `git add <dir>/`**，只列具体文件。
- 新增文件后**必须** `cmake -B build -S .` 重配（engine target 是 `GLOB_RECURSE`）；`opstack-executor-block-tests` 的源文件是**显式列表**（新建 `.cpp` 要手改 `opstack-executor/tests/CMakeLists.txt:146-161`）。
- 计数口径 `Running N test cases`；**判红不得用** `errors detected` 子串；套件名写错 = 0 例假绿。
- 开工基线（Plan A 结束时）：`test-bcos-engine` **344**、`bcos-rpc` **336**、`bcos-evm-opstack-tests` **182**、`opstack-executor-block-tests` **146**、`bcos-evm-eth-tests` **7**、`test-bcos-ledger` 记实测值。**B1 改共享头前后各跑一次并报数。**
- 新断言失败时的纪律：先查 `docs/plans/2026-09-12-m0-cell-audit.md` 与语料 `vectors/DIVERGENCES.md`；都不是就记 finding，**不得改期望表让它变绿**。

## 1. 文件结构

| 文件 | 动作 | 责任 |
|---|---|---|
| `engine/test/unittests/engine/support/OpEngineKarstTestHarness.h` | Modify | B1：9 档 schedule 工厂 + `ImportServiceFixtureT` 的 tag 构造 + `makeNewPayloadAt` |
| `engine/test/unittests/engine/OpEngineForkLadderTest.cpp` | Create | B2：`OpEngineForkLadderSuite` 1 用例 × 9 档 |
| `engine/test/unittests/engine/OpForkNegativeCoverageTest.cpp` | Create | B4：2 个 GAP 用例 + 覆盖表头注释 |
| `opstack-executor/tests/OpT8nReplayTest.cpp` | Modify | B3：逐 fork meta 双向断言（文件内静态函数，不新建文件） |
| `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp` | Modify | B5：9 档写→重开→读回 + genesis 绑定负向 |
| `tools/mutation/variants/{V-LADDER-layoutGuard,V-META-feeArm,V-NEG-order,V-SCHED-genesisBind}.patch` | Create | B6：四个反向变体（判据有区分力的证据） |
| `tools/mutation/variants/mapping.json` | Modify | B6：登记四个变体的 target/filter/also_green |

---

## Task B1: 夹具注入与请求参数化（WI-15）

**Files:**
- Modify: `engine/test/unittests/engine/support/OpEngineKarstTestHarness.h`

- [ ] **Step 1: 加 9 档 schedule 常量与工厂**

插在 `makeKarstProfileSchedule()`（harness `:752-756`）之后：

```cpp
/// The 9-rung ladder schedule: every modeled EL fork on a 1000s grid. The canonical
/// text follows OpForkScheduleCodec's rules (timestamp-0 baseline + strictly
/// contiguous fork order), so it parses through the same production path the ledger
/// uses. Ladder rungs index activations below.
inline constexpr std::string_view c_forkLadderCanonical =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,5000:holocene,"
    "6000:isthmus,7000:jovian,8000:karst";

inline std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> makeForkLadderSchedule()
{
    return std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
        bcos::evm::opstack::OpForkSchedule::parse(c_forkLadderCanonical));
}
```

- [ ] **Step 2: 给 `ImportServiceFixtureT` 加 tag 构造（成员初始化列表里换 seam）**

插在现有 `explicit ImportServiceFixtureT(bcos::protocol::BlockNumber stripImportDeltaAt)` 构造之后（harness `:1184-1192` 一带）：

```cpp
    /// Custom-schedule ctor: the seam can only be swapped in the member-init list
    /// (OpSchedulerSeam deletes copy/move; OpEngineService holds SchedulerType&), so
    /// this is the one place a non-legacy schedule can enter the fixture.
    struct WithSchedule
    {
    };

    explicit ImportServiceFixtureT(
        WithSchedule, std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> schedule)
      : seamScheduler(std::move(schedule), {}),
        delegate(makeImportDelegate<StorageType>(
            /*stripImportDeltaAt=*/-1, blockFactory, storage, ioServicePool)),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        seedGenesisAndForkchoice();
    }
```

> 现有成员 `seamScheduler` 的默认成员初始化**不要动**：既有 344 例全部走默认构造，零影响。
> 参考实现（同仓已有的同类注入点）：`OpServicePair` 的第 4 个构造参数（harness `:737-748`），`KarstProfilePair`（`:832`）就是这么用的。

- [ ] **Step 3: 加按 fork 形状的请求构造器（`makeValidIsthmusNewPayload` 的泛化）**

**先读**：`engine/test/unittests/engine/OpEnginePayloadShapeBaselineTest.cpp`（Plan A WI-08 落的形状基线，4 用例 × 9 格）——它把每个 fork 的 getPayload 包络期望写死在表里，**这就是形状的真值来源**；照它取每档的 `has_withdrawals` / `has_blob_fields` / `extra_data_len`。

然后在 `makeValidIsthmusNewPayload`（harness `:627-655`）之后加：

```cpp
/// Per-fork newPayload request: same skeleton as makeValidIsthmusNewPayload, but the
/// fork-dependent members follow the fork's shape instead of Isthmus's. extraDataLen is
/// 0 pre-Holocene, 9 (00000000fa00000006) for Holocene/Isthmus, 17 for Jovian+ — the
/// shapes pinned by OpEnginePayloadShapeBaselineTest (Plan A WI-08).
inline bcos::engine::NewPayloadRequest makeNewPayloadAt(bcos::protocol::BlockFactory& blockFactory,
    bcos::h256 const& parentHash, bcos::protocol::BlockNumber blockNumber,
    bcos::evm::opstack::OpForkId forkId, bool hasWithdrawals, bool hasBlobFields,
    std::size_t extraDataLen, std::uint64_t timestampMs)
{
    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};  // present-but-empty wire contract
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.blockNumber = blockNumber;
    payload.timestamp = timestampMs;
    payload.gasLimit = 30'000'000;
    payload.gasUsed = 0;
    payload.baseFeePerGas = 1;
    payload.transactions = {};
    if (hasWithdrawals)
    {
        payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
        payload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    }
    if (hasBlobFields)
    {
        payload.excessBlobGas = bcos::u256(0);
        payload.blobGasUsed = bcos::u256(0);
    }
    payload.extraData = bcos::bytes(extraDataLen, 0);
    if (extraDataLen >= 9)
    {
        payload.extraData = bcos::fromHex("00000000fa00000006" +
                                          std::string((extraDataLen - 9) * 2, '0'));
    }
    request.parentBeaconBlockRoot = bcos::h256{};
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        blockFactory.blockHeaderFactory(), payload, txRoot, *request.parentBeaconBlockRoot, forkId);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    return request;
}
```

保留 `makeValidIsthmusNewPayload` 原样（既有多处调用点不动）；如需可再加一层薄包装，但**不要**改它的签名。

- [ ] **Step 4: 编译并报基线数**

```bash
cmake -B build -S . && ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine 2>&1 | grep -m1 -o 'Running [0-9]* test cases'
```
Expected：编译通过；用例数 **344**（与 Plan A 结束一致，未加用例）。

- [ ] **Step 5: Commit**

```bash
git add engine/test/unittests/engine/support/OpEngineKarstTestHarness.h
git commit -m "test(engine): inject a custom fork schedule and per-fork request shapes into the fixture (WI-15)"
```

---

## Task B2: 跨 9 fork 连续链（WI-16）

**Files:**
- Create: `engine/test/unittests/engine/OpEngineForkLadderTest.cpp`
- Modify: 无（CMake 由 GLOB 自动收录，但要重配）

- [ ] **Step 1: 先写 2 档探针，确认"串链"机制成立**

先回答一个问题：**换档之后，下一档的父块是否需要手工登记？** 依据：`seedCanonicalChainABC`（harness `:1142-1160`）用 `validRequest` + `newPayload` + `updateForkchoice` 连推 3 块（且 3 块同属 Isthmus，**从未跨档**）；`buildPayloadAt`（`:807-825`）则是单块驱动、每次重新登记父头。

所以先落这个探针（`OpEngineForkLadderTest.cpp`，只到 Canyon）：

```cpp
#include "support/OpEngineKarstTestHarness.h"
#include <opstack-executor/OpSchedulerSeam.h>  // bcos::evm::engine::detail::tryEngineForkId

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_SUITE(OpEngineForkLadderSuite)

/// Probe: does newPayload+FCU make the next rung's parent header readable on its own,
/// or must the rung register it (registerVerifiedBlock + registerParentHeader)?
BOOST_AUTO_TEST_CASE(TwoRungProbe)
{
    using Fixture = ImportServiceFixture;  // = ImportServiceFixtureT<MLS>（harness:1477-1478）
    Fixture fixture(Fixture::WithSchedule{}, makeForkLadderSchedule());

    // Rung Regolith: ts 0s → block 1; then rung Canyon: ts 1000s → block 2.
    // 断言两块都 Valid，且第二块的 parentHash == 第一块的 blockHash。
    // 若第二块 Invalid/Syncing，把"必须手工登记父头"这一事实写成紧随其后的注释与
    // registerVerifiedBlock/registerParentHeader 调用（照 buildPayloadAt:807-825 的写法）。
}

BOOST_AUTO_TEST_SUITE_END()
```

跑：
```bash
cmake -B build -S . && ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine --run_test=OpEngineForkLadderSuite --log_level=test_suite
```
Expected：探针给出明确答案（绿=可自链；红=需登记，则按红的原因补登记，并把结论写进文件头注释）。

- [ ] **Step 2: 把探针扩成 9 档 rung 表**

rung 表（与 `c_forkLadderCanonical` 一一对应）。**extraData 长度不要手写**：一律从生产常量表
`bcos::engine::extraDataLayoutFor(OpForkId)`（`bcos-framework/bcos-framework/engine/OpForkId.h:113-131`，
并由 `:136-144` 的 static_assert 把九档全钉死）取，`OpExtraDataLayout::Empty=0 / Holocene9=9 / Jovian17=17`。
外部交叉核对用语料 `vectors/DIVERGENCES.md:254`（pre-Cancun 头无 withdrawalsRoot、Canyon 起空 trie 根、blobGasUsed 自 Ecotone 起）。

| rung | fork | 激活 ts(s) | withdrawals | blob 字段 | `extraDataLayoutFor` |
|---|---|---|---|---|---|
| 1 | Regolith | 0 | 否 | 否 | `Empty` |
| 2 | Canyon | 1000 | 是 | 否 | `Empty` |
| 3 | Ecotone | 2000 | 是 | 是 | `Empty` |
| 4 | Fjord | 3000 | 是 | 是 | `Empty` |
| 5 | Granite | 4000 | 是 | 是 | `Empty` |
| 6 | Holocene | 5000 | 是 | 是 | `Holocene9` |
| 7 | Isthmus | 6000 | 是 | 是 | `Holocene9` |
| 8 | Jovian | 7000 | 是 | 是 | `Jovian17` |
| 9 | Karst | 8000 | 是 | 是 | `Jovian17` |

> attrs/请求的**形状**仍按此表构造；**首跑即验**：任何一档的 Invalid 都先查 `docs/plans/2026-09-12-m0-cell-audit.md`，
> 再查 `extraDataLayoutFor`；**不得为了让阶梯变绿而改期望**。

- [ ] **Step 3: 落地 9 档用例（单用例，红了就停）**

```cpp
BOOST_AUTO_TEST_CASE(NineForkLadderIsContinuous)
{
    using Fixture = ImportServiceFixture;  // 需要 cache 读序时换 CacheImportServiceFixture
    Fixture fixture(Fixture::WithSchedule{}, makeForkLadderSchedule());

    struct Rung
    {
        bcos::evm::opstack::OpForkId engineFork;  // 供 rebuildOpEthHeader / API 版本
        std::uint64_t tsSeconds;
        bool hasWithdrawals;
        bool hasBlobFields;
        std::size_t extraDataLen;
    };
    static constexpr Rung c_rungs[] = {
        {bcos::engine::OpForkId::Regolith, 0, false, false, 0},
        {bcos::engine::OpForkId::Canyon, 1000, true, false, 0},
        {bcos::engine::OpForkId::Ecotone, 2000, true, true, 0},
        {bcos::engine::OpForkId::Fjord, 3000, true, true, 0},
        {bcos::engine::OpForkId::Granite, 4000, true, true, 0},
        {bcos::engine::OpForkId::Holocene, 5000, true, true, 9},
        {bcos::engine::OpForkId::Isthmus, 6000, true, true, 9},
        {bcos::engine::OpForkId::Jovian, 7000, true, true, 17},
        {bcos::engine::OpForkId::Karst, 8000, true, true, 17},
    };

    // 期望档位来自测试自己解析的 schedule（独立于服务内部状态）。OpFork → OpForkId 用
    // detail::tryEngineForkId（opstack-executor/OpSchedulerSeam.h:36；Plan A 的两个套件亦用它）。
    auto const ladder = bcos::evm::opstack::OpForkSchedule::parse(c_forkLadderCanonical);

    auto headHash = fixtureHeadHash();
    std::size_t rungsChecked = 0;
    bcos::protocol::BlockNumber height = 0;
    for (auto const& rung : c_rungs)
    {
        ++height;
        BOOST_TEST_INFO_SCOPE("rung " << rungsChecked << " ts=" << rung.tsSeconds
                                      << " fork=" << static_cast<int>(rung.engineFork));
        auto const scheduled = bcos::evm::engine::detail::tryEngineForkId(ladder.forkAt(rung.tsSeconds));
        BOOST_REQUIRE(scheduled.has_value());
        BOOST_CHECK_EQUAL(static_cast<int>(*scheduled), static_cast<int>(rung.engineFork));

        // 逐档 API 版本：取自 profile，不要用字面常量（早期档会走错窗口）。
        auto const profile = bcos::engine::engineApiProfileFor(rung.engineFork);
        auto const fcuVersion = static_cast<std::uint32_t>(profile.forkchoiceUpdated);
        auto const npVersion = static_cast<std::uint32_t>(profile.newPayload);

        auto request = makeNewPayloadAt(*fixture.blockFactory, headHash, height, rung.engineFork,
            rung.hasWithdrawals, rung.hasBlobFields, rung.extraDataLen,
            rung.tsSeconds * 1000ULL + 1);
        auto status = bcos::task::syncWait(fixture.service.newPayload(request, npVersion));
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(status.status), static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

        headHash = request.executionPayload.blockHash;
        bcos::engine::ForkchoiceState fc{headHash, headHash, headHash};
        auto fcu = bcos::task::syncWait(fixture.service.updateForkchoice(fc, nullptr, fcuVersion));
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(fcu.payloadStatus.status), static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        ++rungsChecked;
    }
    BOOST_CHECK_EQUAL(rungsChecked, std::size_t{9});  // 防 0 档假绿
}

BOOST_AUTO_TEST_SUITE_END()
```

> `newPayload`/`updateForkchoice` 的最后一个参数是 **API 版本**：开工时用 `engineApiProfileFor(fork)` 取该档的版本，**不要**沿用这里的字面 `4`（否则早期档会走错窗口；逐档 profile 表在 `bcos-framework/bcos-framework/engine/OpForkId.h:100-111`：Isthmus/Jovian → FCU V3 / getPayload V4 / newPayload V4，Karst → getPayload V5）。`parentHeaderFor(number-1)` + `calcOpBaseFee(*parentHeader, hasDaFootprint)` 需要时照 `validRequest`（harness `:1269-1290`）补；Jovian/Karst 档只允许 deposit 交易（激活窗 deposits-only）。

**把档位钉死（不可省的负控）**：只断言 `Valid` 与 schedule 的档位**证明不了服务真的按档执行**。extraData 布局那一维已被服务自身的校验兜住（`OpEngineService.cpp:170` 拿 `extraDataLayoutFor(forkId)` 校验 payload），所以"传入某档的布局且被接受"已经隐含分类正确；但 withdrawals/blob 两维没有这层兜底。因此每档再补一个**外来布局负控**：同一档、**同一个父块与同一个高度**，只把 extraData 换成另一类布局，必须不被接受。

⚠️ **负控要插在"被接受的请求"之前**，且必须与它同高度：否则拒绝可能来自"高度已占用"或"未知祖先"，而不是布局校验——那样负控是空的（B6 的 `V-LADDER-layoutGuard` 变体会立刻暴露这一点：布局校验被摘掉后，若负控仍因别的原因失败，该变体就不会 RED）。

```cpp
    // 三类布局两两互斥（OpForkId.h:113-131，:136-144 有九档 static_assert）。
    // 长度由布局派生，避免测试自己手写 0/9/17 与生产表漂移。
    constexpr auto extraDataBytesFor = [](bcos::engine::OpExtraDataLayout layout) -> std::size_t {
        return layout == bcos::engine::OpExtraDataLayout::Empty       ? 0U
               : layout == bcos::engine::OpExtraDataLayout::Holocene9 ? 9U
                                                                      : 17U;
    };
```

在 9 档循环里，插在 `newPayload(request, npVersion)` **之前**（同高度、同父块，负控才会只因布局本身被拒）：

```cpp
        // 外来布局负控：同档、同高度、同父块，只有 extraData 布局不同 → 必须不被接受。
        auto const ownLayout = bcos::engine::extraDataLayoutFor(rung.engineFork);
        auto const wrongLayout = ownLayout == bcos::engine::OpExtraDataLayout::Empty
                                     ? bcos::engine::OpExtraDataLayout::Holocene9
                                     : bcos::engine::OpExtraDataLayout::Empty;
        auto wrong = makeNewPayloadAt(*fixture.blockFactory, headHash, height, rung.engineFork,
            rung.hasWithdrawals, rung.hasBlobFields, extraDataBytesFor(wrongLayout),
            rung.tsSeconds * 1000ULL + 2);
        auto const wrongStatus = bcos::task::syncWait(fixture.service.newPayload(wrong, npVersion));
        BOOST_TEST_INFO_SCOPE("negative control: rung " << rungsChecked
                                                       << " wrongLayout=" << static_cast<int>(wrongLayout));
        BOOST_CHECK_MESSAGE(wrongStatus.status != bcos::engine::PayloadValidationStatus::Valid,
            "rung " << rungsChecked << ": a foreign extraData layout must not be accepted");
```

> withdrawals/blob 两维的逐档行为若要更硬的证据，用 `getPayload` 自产路径（服务产出的包络形状）另加断言；本条负控先保证"档位分类是承重的"。Jovian+ 的 DA 等值门（WI-31）在**有用户交易**时才可区分，而激活档只允许 deposit → 该门由 Plan A 的 E7 格负责，此处不重复。

- [ ] **Step 4: 跑并报数**

```bash
cmake -B build -S . && ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine --run_test=OpEngineForkLadderSuite --log_level=test_suite
./build/engine/test/test-bcos-engine 2>&1 | grep -m1 -o 'Running [0-9]* test cases'
```
Expected：套件 1 例 9 档全绿；engine 总数 **344 + 1 = 345**。若某档红：**停下**，报"档位/期望/实测"，按 §0 的纪律记 finding。

- [ ] **Step 5: Commit**

```bash
git add engine/test/unittests/engine/OpEngineForkLadderTest.cpp
git commit -m "test(engine): drive a continuous chain across all nine fork activations (WI-16)"
```

---

## Task B3: 逐 fork 收据 meta 形状（WI-17）

**Files:**
- Modify: `opstack-executor/tests/OpT8nReplayTest.cpp`（**不新建文件**：`loadBlockContext`/`replaySingleBlockInto`/`DivergenceLedger` 都是该文件的文件内静态函数）

**为什么在执行路径上做**：RPC 层只按 `optional` 存在与否拷贝、**不看 fork**（`bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp:100-140`），所以逐 fork 字段只能在产出 meta 的那一层断言。

- [ ] **Step 1: 定位插入点**

```bash
grep -n 'replaySingleBlockInto\|result.receipts\|result.txTypes\|kDepositTxType' opstack-executor/tests/OpT8nReplayTest.cpp | head
```
Expected（实测）：函数在 `:894`；回执循环在 `:1055-1122`，其中 `:1058-1059` 已算出 `receipt` 与 `isDeposit`。**断言就插在这个循环里**（`bc.cfg`、`result.receipts`、`result.txTypes` 都在手边）。

- [ ] **Step 1b: 先与外部 oracle 对账（不可省，否则期望就是"照被测代码抄一份"）**

外部依据：语料 `opstack-executor/tests/t8n/vectors/OP_RECEIPT_FIELDMAP.md`（探针实测）。两条**会被写错**的地方必须先处理：

1. **deposit 回执是"早退"形态**（FIELDMAP §4.1，op-geth `receipt_opstack.go:36-38`：最后一笔是 deposit 就整体早退）：op-geth 的 deposit 回执**只发** `DepositNonce` + `DepositReceiptVersion`，L1/fee/operator/DA 字段**全部 absent**。**FISCO 侧已同构、无需立案**：`deriveOpReceiptMeta` 只经 `opTransition`（非 deposit）可达，deposit 走 `runDeposit` 直接构造 deposit 字段（FIELDMAP §6 原文与 `OpTransition.cpp:223` 的补算路径），FIELDMAP §7.2 又要求探针必须覆盖 deposit 分支——所以 §4.1 的早退形态对两侧都成立，直接写进期望即可，**不要**再按"可能偏离"去立案。
2. **字段存在性是"值相关"的，规则是精确的**（FIELDMAP §5.4）：op-geth `receipt_opstack.go:44` 只在 `operatorFeeScalar != 0 || operatorFeeConstant != 0` 时写这两个字段，生成器 `buildExpectedReceipts` 同源镜像。所以只看 `cfg.has_operator_fee` **不够**：期望集必须由"该档 + 该向量的实际 scalar/constant"共同决定（§4.3 的 `da_mix` scalar=0 → absent；§4.2 的 `fee_env_observer` 5000/7777 → present）。DA 侧同理（`da_footprint*` 在 Jovian+ 且 scalar 非 0 时出现）。
3. **Ecotone 档只钉"存在性"，不钉值**：FIELDMAP §6 记着一条**已知的、gate 不可见的**值分叉——Ecotone 下 FISCO 的 `l1_gas_used` 补算走 `estimatedDaSizeScaled(0)*16/1e6 = 1600`，而 op-geth 用 `bedrockCalldataGasUsed`；34 个向量全是 isthmus/jovian（Fjord+），**Ecotone 无向量覆盖**。B3 断言的是字段集（两侧该字段都存在），所以这条分叉**不会**让 B3 变红——必须在测试头注释里写明"Ecotone 只验存在性，值分叉见 FIELDMAP §6 归线 A"，避免后来者误以为值也验过了。

**对账动作**：把 `actualMetaFields` 先接进 `_deposit_only` 的 8 档重放，把每档实际集合打印出来，与 FIELDMAP §4.1/§4.2/§4.3 逐项比对；差异分三类处置——(a) 已在 `DIVERGENCES.md` 登记 → 在期望里注明来源；(b) 未登记且属我方有意偏离 → 立案；(c) 无说法 → finding。**对账结论写进本文件的步骤记录**（哪档哪字段、结论、依据）。

**两档硬锚（把外部依据钉进测试）**——非 deposit 回执：

| 档 | 期望字段集（非 deposit） | 依据 |
|---|---|---|
| Isthmus | `l1_gas_price`、`l1_blob_base_fee`、`l1_gas_used`、`l1_fee`、`l1_base_fee_scalar`、`l1_blob_base_fee_scalar`、`operator_fee_scalar`、`operator_fee_constant`（`l1_fee_scalar` 缺席、`da_footprint*`/`da_footprint_gas_scalar` 缺席） | FIELDMAP §4.2（`fee_env_observer` 实测） |
| Jovian | 上列 + `da_footprint_gas_scalar`、`da_footprint`（`operator_fee_scalar/constant` 仅在 scalar≠0 时 present） | FIELDMAP §4.3（`da_mix` 实测，DA scalar=400，`BlobGasUsed=0x9c40/0x1db00/0x69780`） |

> `_op_operator_fee`（聚合）在 op-geth 侧**没有对应字段**（FIELDMAP §5.2：FISCO 用 `operatorFee()` 公式手算）——它是 FISCO 派生字段，**不要**在注释里声称有 op-geth 背书；`da_footprint` 则有（§5.3：即 op-geth 的 `BlobGasUsed`）。

- [ ] **Step 2: 加期望/实际两个 helper 与计数器**

放在该文件的匿名命名空间（`replaySingleBlockInto` 之前）：

```cpp
/// 期望集 = OpForkConfig × 该笔的**实际取值**。两点必须记住（FIELDMAP 实测）：
/// ① deposit 回执是"早退"形态，只发两个 deposit 字段（§4.1，receipt_opstack.go:36-38）；
/// ② 零值 scalar 不发对应字段（§4.3：jovian da_mix 的 operator scalar=0 ⇒ 那两个 absent）。
/// 这两条决定**期望**，但仍不从被测代码里读。
struct MetaExpectation
{
    bcos::evm::opstack::OpForkConfig const& cfg;
    bool isDeposit;
    // op-geth receipt_opstack.go:44 —— 只在 `scalar != 0 || constant != 0` 时才写
    // OperatorFeeScalar/Constant（FIELDMAP §5.4）。按该向量/配置的实际取值传，不要恒 true。
    bool operatorFeeEmitted;
    // DA 侧同理（FIELDMAP §5.3）：Jovian+ 且 scalar 非 0 才写 da_footprint_gas_scalar/da_footprint。
    bool daScalarNonZero;
};

std::set<std::string> expectedMetaFields(MetaExpectation const& in)
{
    std::set<std::string> fields;
    if (in.isDeposit)
    {
        fields.insert("deposit_nonce");
        // Regolith 的 deposit 回执不带 version（op-geth 从 Canyon 起写）——已登记偏离，
        // 出处：opstack-executor/tests/t8n/vectors/DIVERGENCES.md:255。
        // deposit 是早退形态、只两个字段：FISCO 侧同构（FIELDMAP §6：deposit 走 runDeposit），
        // 不是偏离，不要为此立案。
        if (in.cfg.fork != bcos::evm::opstack::OpFork::Regolith)
        {
            fields.insert("deposit_receipt_version");
        }
        return fields;
    }
    fields.insert({"l1_gas_price", "l1_gas_used", "l1_fee"});
    if (in.cfg.l1_fee_model == bcos::evm::opstack::L1FeeModel::Bedrock)
    {
        fields.insert("l1_fee_scalar");
    }
    else
    {
        fields.insert({"l1_blob_base_fee", "l1_base_fee_scalar", "l1_blob_base_fee_scalar"});
    }
    if (in.cfg.has_operator_fee && in.operatorFeeEmitted)
    {
        fields.insert({"operator_fee_scalar", "operator_fee_constant", "operator_fee"});
    }
    if (in.cfg.has_da_footprint && in.daScalarNonZero)
    {
        fields.insert({"da_footprint_gas_scalar", "da_footprint"});
    }
    return fields;
}

/// Step 1b 对账用：把字段集打成人可读的一行。
std::string joinFields(std::set<std::string> const& fields)
{
    std::string out;
    for (auto const& f : fields)
    {
        if (!out.empty())
        {
            out += ",";
        }
        out += f;
    }
    return out;
}

std::set<std::string> actualMetaFields(bcos::protocol::TransactionReceipt const& receipt)
{
    std::set<std::string> fields;
    auto const meta = receipt.opStackMeta();  // std::optional<OpStackReceiptMeta>, 14 fields
    if (!meta)
    {
        return fields;
    }
    if (meta->l1_gas_price) fields.insert("l1_gas_price");
    if (meta->l1_gas_used) fields.insert("l1_gas_used");
    if (meta->l1_fee) fields.insert("l1_fee");
    if (meta->l1_fee_scalar) fields.insert("l1_fee_scalar");
    if (meta->l1_blob_base_fee) fields.insert("l1_blob_base_fee");
    if (meta->l1_base_fee_scalar) fields.insert("l1_base_fee_scalar");
    if (meta->l1_blob_base_fee_scalar) fields.insert("l1_blob_base_fee_scalar");
    if (meta->operator_fee_scalar) fields.insert("operator_fee_scalar");
    if (meta->operator_fee_constant) fields.insert("operator_fee_constant");
    if (meta->operator_fee) fields.insert("operator_fee");
    if (meta->da_footprint_gas_scalar) fields.insert("da_footprint_gas_scalar");
    if (meta->da_footprint) fields.insert("da_footprint");
    if (meta->deposit_nonce) fields.insert("deposit_nonce");
    if (meta->deposit_receipt_version) fields.insert("deposit_receipt_version");
    return fields;
}

/// Proof the per-fork assertion actually ran (not a 0-cell green).
std::size_t g_metaForkCellsChecked = 0;
```

- [ ] **Step 3: 在回执循环里插入双向断言**

紧跟 `:1058-1059`（`const auto& receipt = ...; const bool isDeposit = ...;`）之后：

```cpp
        if (id.find("_deposit_only") != std::string::npos)
        {
            // operatorFeeEmitted / daScalarNonZero 来自该向量的配置与实际取值（不是从
            // receipt meta 反推，否则就是同义反复）；Step 1b 的对账结论决定这两个实参。
            auto const got = actualMetaFields(receipt);
            auto const want = expectedMetaFields(
                {bc.cfg, isDeposit, /*operatorFeeEmitted=*/true, /*daScalarNonZero=*/true});
            BOOST_TEST_INFO_SCOPE(id << " receipt[" << i << "] fork=" << static_cast<int>(bc.cfg.fork)
                                     << " want=" << joinFields(want) << " got=" << joinFields(got));
            BOOST_CHECK_MESSAGE(
                got == want, id << ": receipt meta field set mismatch (both directions checked)");
            ++g_metaForkCellsChecked;
        }
```

> 两个 `*NonZero` 实参**按 Step 1b 的实测结论填**，不要一律写 `true`：若该档该向量确实带 0 值 scalar，期望里就不能有对应字段（FIELDMAP §4.3 的教训）。

- [ ] **Step 4: 在向量重放入口用例末尾加计数器守卫**

入口用例是 `Vectors`（`opstack-executor/tests/OpT8nReplayTest.cpp:1420`）；在其返回前加：

```cpp
    // 8 = Regolith…Jovian; Karst has no *_deposit_only vector (registered gap: Plan C/corpus).
    BOOST_CHECK_MESSAGE(
        g_metaForkCellsChecked >= 8, "expected >= 8 per-fork meta cells, got " << g_metaForkCellsChecked);
```

- [ ] **Step 5: 跑并报数**

```bash
ninja -C build opstack-executor-block-tests
./build/opstack-executor/tests/opstack-executor-block-tests --run_test=OpT8nReplayTest/ --log_level=test_suite
./build/opstack-executor/tests/opstack-executor-block-tests 2>&1 | grep -m1 -o 'Running [0-9]* test cases'
```
Expected：全绿，用例数 **146**（本 Task 只加断言不加用例）。若 Regolith/Canyon 档红：先查语料 `vectors/DIVERGENCES.md` 是否已登记该档字段差；登记过就在 `expectedMetaFields` 里注明来源，未登记即 finding。

- [ ] **Step 6: Commit**

```bash
git add opstack-executor/tests/OpT8nReplayTest.cpp
git commit -m "test(executor): pin the per-fork receipt meta field set on the execution path (WI-17)"
```

---

## Task B4: F8 负向格（WI-18）

**Files:**
- Create: `engine/test/unittests/engine/OpForkNegativeCoverageTest.cpp`

**范围（实测，取代规格的推断）**：四类负向面里只剩 **2 个 GAP**——OP 侧 stale head 的 FCU 语义、OP 侧多字段首错顺序。其余（方法窗外版本、错误码路由、deposits-only、latestValidHash）已有覆盖，覆盖表（含 file:line）来自规格「B4 补齐」节，**必须抄进本文件的头注释**作为验收物。

- [ ] **Step 1: 读 Eth 侧的同义用例与 OP 侧顺序契约**

```bash
sed -n '677,700p' engine/test/unittests/engine/EngineServiceTest.cpp
sed -n '337,361p' engine/bcos-engine/OpEngineService.cpp
```
Expected：拿到 stale-head 的断言形态（tip 不后退、`latestValidHash` 取值）与首错顺序表（transactions → withdrawals → blobVersionedHashes → windowFields → headerFields → blobGasUsed，且 window 在 header 之前）。

- [ ] **Step 2: 落两个用例**

```cpp
#include "support/OpEngineKarstTestHarness.h"

using namespace op_engine_parity_test;

// fork × negative-surface coverage (measured; only the two GAPs below are new):
//   方法窗外版本 → UnsupportedFork : OpEngineApiVersionsTest.cpp:83/128/385/429,
//                                    OpEngineServiceParityTest.cpp:102/394/1083,
//                                    EngineServiceTest.cpp:412/436/457/1812
//   错误码路由(−32603/Invalid/Syncing) : OpEngineServiceParityTest.cpp:671/972/1029/1149/1303/1364
//   deposits-only : OpJovianShapeTest.cpp:57(c), PreBlockOpStepsTest.cpp:274/662/731,
//                   OpL1BlockDepositTest.cpp:750   (Jovian+Karst only; pre-Jovian not applicable)
//   latestValidHash : OpEngineServiceParityTest.cpp:431/638/1149, OpEngineImportFcuTest.cpp:52/90/527,
//                     OpEngineServiceExecParityTest.cpp:347/392, OpNewPayloadRpcE2eTest.cpp:644/656/1323
//   GAP 1 stale head (OP)  — this file
//   GAP 2 多字段首错顺序 (OP) — this file
//
// SCOPE OF THIS TABLE (do not read it as exhaustive):
//   * four surfaces only — code routing, first-error order, deposits-only, latestValidHash.
//     Not swept: blob versioned-hash handling, extraData shape, deposit tolerance, DA gates.
//   * the code-routing and latestValidHash rows are measured on Isthmus/default only
//     (the OP cases listed are Isthmus-timestamped); no per-fork sweep exists for them.
//   * "covered" describes timestamp classification: a listed case runs with the fork's
//     timestamp, it is not a per-fork × per-surface cross product.

BOOST_AUTO_TEST_SUITE(OpForkNegativeCoverageSuite)

/// GAP 1: same semantics as the Eth-side forkchoice_ignores_stale_update_after_newer_head_wins
/// (EngineServiceTest.cpp:677) — a later FCU pointing at an older head must not rewind the tip.
BOOST_AUTO_TEST_CASE(OPStaleHeadForkchoiceDoesNotRewindTheTip)
{
    // 照 EngineServiceTest.cpp:677 的三步形态，用 ImportServiceFixtureT（默认 Isthmus schedule）：
    // ① seedCanonicalChainABC()（harness:1142）拿到 height 1/2/3 的哈希；
    // ② 对 height 3 发 FCU（当前 tip）；
    // ③ 再对 height 1 发 FCU，断言 payloadStatus == Valid 且 tip 仍是 height 3
    //    （通过 fixture.service.getBlockNumber() 或等价读取），latestValidHash 不留回退痕迹。
}

/// GAP 2: the first-error order contract (OpEngineService.cpp:337-361) was comment-only on the
/// OP path; pin it by malforming TWO fields and asserting the message names the FIRST one.
BOOST_AUTO_TEST_CASE(OPFirstErrorOrderPrefersTransactionsOverHeaderFields)
{
    // 构造一个同时非法的请求：transactions 清空 + parentBeaconBlockRoot 清零，
    // 断言拒绝消息命中 transactions 分支（顺序表第一位），而不是 header 分支。
    // 断言用具体消息子串，不用"抛异常"。
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 3: 重配 + 跑 + 报数**

```bash
cmake -B build -S . && ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine --run_test=OpForkNegativeCoverageSuite --log_level=test_suite
./build/engine/test/test-bcos-engine 2>&1 | grep -m1 -o 'Running [0-9]* test cases'
```
Expected：2 例全绿；engine 总数 **345 + 2 = 347**（新文件必须重配，否则 suite 根本不编译 = 0 例假绿）。

- [ ] **Step 4: Commit**

```bash
git add engine/test/unittests/engine/OpForkNegativeCoverageTest.cpp
git commit -m "test(engine): fill the two per-fork negative cells that had no coverage (F8, WI-18)"
```

---

## Task B5: 调度持久化（WI-20）

**Files:**
- Modify: `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp`（**不新建文件**）

**范围收窄（实测，取代规格的 Step 2）**：该文件**已有** 6 个用例，其中
`genesisPersistsScheduleMetadataTriple:98`（创世写→读回→文本/genesisHash/**硬编码 keccak**）、
`hashMismatchFailClosed:130`（keccak 改写 → `"hash mismatch"`，resolve 与 read 两条路径）、
`genesisBranchReturnsNormalizedCanonical:174`、`storedBranchReturnsNormalizedCanonical:181`、
`emptyMetadataFallsBackToLegacy:204`、`storedScheduleDivergesFromGenesis:212`。
所以**不要**重写 keccak 负向（已覆盖），只补两处真缺口：**9 档 schedule** 与 **重开**（现有读路径复用同一个 storage 实例，未模拟重启）。

- [ ] **Step 1: 读既有形态**

```bash
sed -n '96,130p' bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp
```
Expected：拿到 `makeL2GenesisTestStorage()` / `scheduleGenesis(text)` / `buildGenesisBlock` / `readOpForkScheduleMetadata(*storage, genesisHash)` 的用法与 fixture 结构。

- [ ] **Step 2: 加 9 档 + 重开用例**

```cpp
// 需要 #include <set>（`std::set`）。canonical 与引擎侧阶梯同一文本。
constexpr char const* c_nineForkLadderSchedule =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,5000:holocene,"
    "6000:isthmus,7000:jovian,8000:karst";

BOOST_AUTO_TEST_CASE(nineForkScheduleSurvivesAReopen)
{
    task::syncWait([this]() -> task::Task<void> {
        // "重开"需要两个 storage 对象压同一个 backing store —— 所以显式搭栈，不用
        // makeL2GenesisTestStorage()（它把 backing 藏在函数里）。L2GenesisTestStorage 的
        // 构造接 prev/backing（L2GenesisTestStorage.h:32）；丢掉前台对象、backing 存活，
        // 对读取路径而言就是一次进程重启。
        auto backing = std::make_shared<storage::StateStorage>(nullptr, false);
        backing->setEnableTraverse(true);
        auto storage = std::make_shared<L2GenesisTestStorage>(backing);
        storage->setEnableTraverse(true);

        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(c_nineForkLadderSchedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        const auto before = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(before.has_value());
        BOOST_CHECK_EQUAL(before->schedule, c_nineForkLadderSchedule);

        // 9 档 canonical 的 keccak 要**硬编码**（F12：期望不拿被测函数自己推）。取法：先加一行
        // scratch 探针打印 keccakOpForkScheduleHash(c_nineForkLadderSchedule).hex()，把结果
        // 粘成下面的常量，再按 :117-121 的写法**双向**断言（常量 vs metadata vs 函数结果）。
        constexpr char const* c_nineForkLadderHash = /* 首次运行后填入 64 位 hex 常量 */ "";
        BOOST_CHECK_EQUAL(before->scheduleHash.hex(), c_nineForkLadderHash);
        BOOST_CHECK_EQUAL(
            keccakOpForkScheduleHash(c_nineForkLadderSchedule).hex(), c_nineForkLadderHash);

        // 重开的读取路径：新对象、同一 backing。
        auto reopened = std::make_shared<L2GenesisTestStorage>(backing);
        reopened->setEnableTraverse(true);
        const auto after = co_await readOpForkScheduleMetadata(*reopened, ledgerGenesisHash);
        BOOST_REQUIRE(after.has_value());
        BOOST_CHECK_EQUAL(after->schedule, before->schedule);
        BOOST_CHECK_EQUAL(after->scheduleHash.hex(), before->scheduleHash.hex());
        BOOST_CHECK_EQUAL(after->genesisHash.hex(), before->genesisHash.hex());

        // 9 条边界重开后仍解析出 9 个互不相同的档位。
        auto const ladder = bcos::evm::opstack::OpForkSchedule::parse(after->schedule);
        std::set<int> forks;
        for (std::uint64_t const ts : {0ULL, 1000ULL, 2000ULL, 3000ULL, 4000ULL, 5000ULL,
                 6000ULL, 7000ULL, 8000ULL})
        {
            forks.insert(static_cast<int>(ladder.forkAt(ts)));
        }
        BOOST_CHECK_EQUAL(forks.size(), std::size_t{9});
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisBindingMismatchFailsClosed)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(c_nineForkLadderSchedule), emptyLedgerConfig()));

        // 与该行绑定的 genesis 不同的哈希（全 'f' 不可能真实出现）。
        HashType const wrongGenesis(std::string(64, 'f'));
        bool threw = false;
        try
        {
            (void)co_await readOpForkScheduleMetadata(*storage, wrongGenesis);
        }
        catch (InvalidOpForkSchedule const& e)
        {
            threw = true;
            BOOST_CHECK(messageContains(e, "genesis binding mismatch"));
        }
        BOOST_CHECK(threw);
        co_return;
    }());
}
```

> 异常断言用 `try/catch` 而**不是** `BOOST_CHECK_EXCEPTION`：读取是协程，异常必须在本协程内捕获——既有 `hashMismatchFailClosed:159-169` 就是这一形态（照它写）。

- [ ] **Step 3: 跑并报数**

```bash
ninja -C build test-bcos-ledger
./build/test/bcos-ledger/test-bcos-ledger --run_test=OpForkScheduleMetadataTest --log_level=test_suite
./build/test/bcos-ledger/test-bcos-ledger 2>&1 | grep -m1 -o 'Running [0-9]* test cases'
```
Expected：套件 **6 + 2 = 8** 例全绿，总数只增不减。若 `genesisBindingMismatchFailsClosed` 没抛（生产无该守卫）→ **停下记 finding**，不在 B 里补生产。

- [ ] **Step 4: Commit**

```bash
git add bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp
git commit -m "test(ledger): pin a nine-fork schedule across a ledger reopen + the genesis-binding rejection (WI-20)"
```

---

## Task B6: 变异/反向验证链（判据有区分力的证据，WI-15/16/17/18/20 共用）

**为什么必须有**：Plan A 对每条新断言都用 `tools/mutation/` 证明"把生产判断反向改一处，对应断言必红"（现有 9 个变体）。没有这条证据，"新断言"可能只是**恒真**的陪衬。B 的 4 组新判据各配一个变体。

**Files:**
- Create: `tools/mutation/variants/V-LADDER-layoutGuard.patch`（B2）
- Create: `tools/mutation/variants/V-META-feeArm.patch`（B3）
- Create: `tools/mutation/variants/V-NEG-order.patch`（B4）
- Create: `tools/mutation/variants/V-SCHED-genesisBind.patch`（B5）
- Modify: `tools/mutation/variants/mapping.json`（追加 4 条）

**工作流（harness 的既有用法，`make-variant.sh` 头注释即此）**：手工改**一处**生产判断 → `bash tools/mutation/make-variant.sh <ID> <mutated-file>`（写补丁并 `git checkout` 复原）→ 在 `mapping.json` 追加条目 → `bash tools/mutation/run.sh <ID>`（必须 RED，且 `also_green` 负控保持 GREEN）。

- [ ] **Step 1: V-LADDER-layoutGuard（B2）**

反向的判断：`engine/bcos-engine/OpEngineService.cpp:170` 的 extraData 布局校验（`validateOpExtraDataForLayout(payload.extraData, extraDataLayoutFor(forkId))`）——把它改成"总是通过"。这样 B2 每档的**外来布局负控**会被接受，负控断言必红。

```bash
# 手改 :170 一处，令其不返回错误
bash tools/mutation/make-variant.sh V-LADDER-layoutGuard engine/bcos-engine/OpEngineService.cpp
```

`mapping.json` 追加：

```json
{
  "variant": "V-LADDER-layoutGuard",
  "finding": "WI-16",
  "target": "test-bcos-engine",
  "binary": "engine/test/test-bcos-engine",
  "filter": "OpEngineForkLadderSuite/NineForkLadderIsContinuous",
  "also_green": ["OpEngineSequenceMatrixSuite/S3_SameHeightSwitchDropsSibling"],
  "expect": "only_mapped_red",
  "why": "布局校验被摘除后，阶梯的逐档外来布局负控必须失败；also_green 取同 binary 里与该机制无关的既有用例（该名字在 mapping.json 的 N1/N2 条目里已在用，直接抄，不要自己编）。"
}
```

> `also_green` 一律用**已登记**的既有用例名（本文件或 `mapping.json` 里出现过），避免"编一个不存在的用例 → 负控假绿"。

- [ ] **Step 2: V-META-feeArm（B3）**

先定位 meta 的产出点：

```bash
grep -rn 'opStackMeta\|OpStackReceiptMeta' --include='*.cpp' --include='*.h' opstack-executor engine | grep -v tests | head
```
Expected：找到把 fee 臂字段写进 meta 的那处（Bedrock 写 `l1_fee_scalar`，Ecotone+ 写 scalar 对）。反向它一处（例如让 Bedrock 臂也写 `l1_blob_base_fee`），B3 的 Regolith/Canyon 档期望必然不匹配 → RED。

```bash
bash tools/mutation/make-variant.sh V-META-feeArm <上一步定位到的文件>
```

`mapping.json` 追加（`target`/`binary` 换 executor 的）：

```json
{
  "variant": "V-META-feeArm",
  "finding": "WI-17",
  "target": "opstack-executor-block-tests",
  "binary": "opstack-executor/tests/opstack-executor-block-tests",
  "filter": "OpT8nReplayTest/Vectors",
  "also_green": ["GoldenSampleSuite/ManifestCorpusConsistency"],
  "expect": "only_mapped_red",
  "why": "fee 臂写错一个字段 ⇒ 逐 fork meta 字段集双向断言必红；语料一致性用例与 meta 形状无关，须保持 GREEN。"
}
```

- [ ] **Step 3: V-NEG-order（B4）**

反向的判断：`engine/bcos-engine/OpEngineService.cpp:337-361` 的首错顺序——把其中两条相邻检查**交换**（例如 window 与 header 对调）。B4 的 `OPFirstErrorOrderPrefersTransactionsOverHeaderFields` 断言的正是"哪一条先触发"，交换后消息变了 → RED。

```bash
bash tools/mutation/make-variant.sh V-NEG-order engine/bcos-engine/OpEngineService.cpp
```

`mapping.json` 追加（`filter` = `OpForkNegativeCoverageSuite/OPFirstErrorOrderPrefersTransactionsOverHeaderFields`，`also_green` = `OpForkNegativeCoverageSuite/OPStaleHeadForkchoiceDoesNotRewindTheTip`——后者与本机制无关，交换首错顺序后仍须 GREEN）。

- [ ] **Step 4: V-SCHED-genesisBind（B5）**

反向的判断：`bcos-framework/bcos-framework/ledger/ChainMetadata.h:101-124` 的 genesis 绑定校验——把它改成"比较结果恒为真"（**保留 `throw` 语句本身**，只让条件不成立；直接删 `throw` 可能让后续代码被判不可达而编译失败）。B5 的 `genesisBindingMismatchFailsClosed` 必红。

> 这是**头文件**改动：`make-variant.sh` 的第二参就是给这种文件用的（默认值是 `.inl`，这里必须显式传 `.h`）；代价是该目标的较大重编。

```bash
bash tools/mutation/make-variant.sh V-SCHED-genesisBind bcos-framework/bcos-framework/ledger/ChainMetadata.h
```

`mapping.json` 追加（`target` = `test-bcos-ledger`，`binary` = `test/bcos-ledger/test-bcos-ledger`，`filter` = `OpForkScheduleMetadataTest/genesisBindingMismatchFailsClosed`，`also_green` 取 `OpForkScheduleMetadataTest/hashMismatchFailClosed`——两者机制相邻但独立，正好当负控）。

- [ ] **Step 5: 跑全部四个变体并记录**

```bash
bash tools/mutation/run.sh V-LADDER-layoutGuard V-META-feeArm V-NEG-order V-SCHED-genesisBind
for id in V-LADDER-layoutGuard V-META-feeArm V-NEG-order V-SCHED-genesisBind; do
  git checkout -- $(git apply --numstat "$PWD/tools/mutation/variants/$id.patch" | awk '{print $3}' | head -1)
done
git status --porcelain   # 必须为空（harness 自行复原；这里只做兜底）
```
Expected：四个都 RED 且被归因（`also_green` 全绿）；`git status` 干净。任一为 GREEN = 该判据**没在守任何东西**，按 finding 记下并重写断言。

- [ ] **Step 6: Commit**

```bash
git add tools/mutation/variants/V-LADDER-layoutGuard.patch \
        tools/mutation/variants/V-META-feeArm.patch \
        tools/mutation/variants/V-NEG-order.patch \
        tools/mutation/variants/V-SCHED-genesisBind.patch \
        tools/mutation/variants/mapping.json
git commit -m "test(mutation): add the four reverse-fix variants for the fixture-layer assertions (WI-15..20)"
```

---

## 2. 自检（已跑，含代码级修正与一轮"覆盖/正确性"审查的收敛）

**审查（覆盖 + 正确性）后新增/改写的内容**（本轮，按发现顺序）：
1. B2 加**逐档外来布局负控**（把"档位分类是承重的"变成可证伪的断言），并把 rung 表的 extraData 长度改为从 `extraDataLayoutFor`（`OpForkId.h:113-131`，九档 static_assert 在 `:136-144`）派生，不再声称出自形状基线测试（那个文件只钉 18 键包络，不含逐档长度——原引用是错的）。
2. B3 新增 **Step 1b：与语料 `OP_RECEIPT_FIELDMAP.md` 对账**，并据此改掉两处会写错的期望——① deposit 回执是 op-geth 的**早退形态**（只两个 deposit 字段，§4.1），FISCO 若不同必须先立案再写进期望；② 字段存在性对**零值 scalar** 敏感（§4.3 实测 operator scalar=0 ⇒ 该两字段 absent），故期望集要由"档 + 实际 scalar 值"共同决定，`MetaExpectation` 因此带上两个 `*NonZero`。另按 §4.2/§4.3 硬钉 isthmus/jovian 两档字段集，并标注 `operator_fee*` 无 op-geth 背书、`da_footprint` 以 `BlobGasUsed` 为载体。
3. B4 的头注释补 **SCOPE 段**（只扫 4 个面、两行实测仅在 Isthmus/默认档、非逐 fork 叉乘），避免被当穷举读。
4. B5 的 9 档用例加**硬编码 keccak**（F12 写法，双向断言）。
5. 新增 **Task B6**：四个反向变体（V-LADDER-layoutGuard / V-META-feeArm / V-NEG-order / V-SCHED-genesisBind）+ `mapping.json` 登记，要求全部 RED 且被归因——补上 Plan A 有、Plan B 原本缺的"判据有区分力"证据链。
6. §3 验收同步扩写（Karst 8/9 的后果必须显式处置、deposit 形态定性、外部锚、B6 证据）。

**第二轮审查（对上一轮新增内容本身）发现并修正的 5 处**：
1. **B2 负控会被"别的原因"满足**：初稿把外来布局负控放在被接受请求**之后**、复用刚提交的高度 ⇒ 拒绝可能来自"高度已占用"而非布局校验，负控变空，B6 的 `V-LADDER-layoutGuard` 也就不会红。已改为**插在被接受请求之前、同高度同父块**，并写明这个陷阱与后果。
2. **B2 代码仍留字面 API 版本 `4`**：与紧邻的说明自相矛盾。已改为逐档 `bcos::engine::engineApiProfileFor(rung.engineFork)` 取 `newPayload`/`forkchoiceUpdated` 版本（`OpEngineApiMatrixTest.cpp:67` 就是这么用的）。
3. **B3 关于 deposit 的处置写反了**：初稿要求"FISCO 若不同则立案"，但 FIELDMAP §6 已明确 FISCO 侧 `deriveOpReceiptMeta` 只经 `opTransition`（非 deposit）可达、deposit 走 `runDeposit` 只构造 deposit 字段 ⇒ **两侧同构、无需立案**。已改为直接引用 §6 并显式写"不要为此立案"，避免实施者凭空立一条不存在的偏离。
4. **operator 的发射规则不精确**：`MetaExpectation.operatorScalarsNonZero` 现按 FIELDMAP §5.4 的精确规则（`scalar != 0 || constant != 0`）改名为 `operatorFeeEmitted`，并同步改注释与调用点。
5. **B6 的 `also_green` 有省略号 / 未定名**：这是会"编一个不存在的用例 ⇒ 负控假绿"的地方。`.patch` 的 `also_green` 已改为**已登记**的既有用例名（`OpEngineSequenceMatrixSuite/S3_SameHeightSwitchDropsSibling`，N1/N2 条目在用；`V-NEG-order` 用 `OPStaleHeadForkchoiceDoesNotRewindTheTip`）；V-SCHED 的变异说明也补上"改头文件要保持可编译、别删 `throw`"。

另外记一条**新知识**（不进验收断言，只入注释）：FIELDMAP §6 的 Ecotone 值分叉（FISCO `l1_gas_used` 走 `estimatedDaSizeScaled(0)*16/1e6=1600`，op-geth 走 `bedrockCalldataGasUsed`）**B3 验不到**（只验存在性），已在 B3 Step 1b 第 3 条要求写进测试头注释，免得被误读成"值也验过了"。

**此前的修正**：
- **规格覆盖**：WI-15→B1、WI-16→B2、WI-17→B3、WI-18→B4、WI-20→B5，无遗漏；规格的 `文件结构` 表已被本文件 §1 取代（B3/B5 改为 Modify）。
- **占位符**：唯一需要现场取值的是 B6 的 `also_green` 用例名（用 `--list_content` 挑，已写明怎么挑）；B5 的 keccak 常量必须先测后钉（已写明取法）。这两处是**必须实测的值**，不是未定的设计。
- **类型/名称一致性**：`makeForkLadderSchedule` / `c_forkLadderCanonical` / `makeNewPayloadAt` / `ImportServiceFixtureT::WithSchedule` / `MetaExpectation` / `expectedMetaFields` / `actualMetaFields` / `joinFields` / `g_metaForkCellsChecked` 在全文用同一套名字。
- **代码级修正（初稿会编不过/不成立）**：
  1. 夹具实例化：`ImportServiceFixtureT<CacheMemStorage>` 不存在 → 用既有别名 `ImportServiceFixture`（= `ImportServiceFixtureT<MLS>`，harness `:1477-1478`）。
  2. 档位类型：`forkAt` 返回 `OpFork`，请求构造要 `OpForkId` → rung 表存 `OpForkId`，并用 `bcos::evm::engine::detail::tryEngineForkId`（`opstack-executor/OpSchedulerSeam.h:36`）映射后比对。
  3. B5 的"重开"初稿只是同一对象读两次 → 改为**显式搭栈**：一个 backing `storage::StateStorage` + 两个 `L2GenesisTestStorage`（构造接 backing，`L2GenesisTestStorage.h:32`）；负向断言改为协程内 `try/catch`（照既有 `hashMismatchFailClosed:159-169`）。

## 3. 验收（Plan B 完成 = ）

- [ ] `ImportServiceFixtureT` 有 `WithSchedule` 构造且被阶梯用例实际使用；engine 用例数 **344 → 347**（+B2 1、+B4 2），无回归。
- [ ] 阶梯 1 用例 × 9 档全绿；或每处失败都已在 M0 台账/finding 里记录（**不改 rung 表求绿**）。
- [ ] **阶梯的档位是承重的**：每档的**外来 extraData 布局负控**（同档同父块换一类布局）必须**不被接受**；rung 表的 extraData 长度取自 `extraDataLayoutFor` 而不是手写常数。
- [ ] 逐 fork meta 字段集**双向**断言落位，`g_metaForkCellsChecked >= 8`；**Karst 无 `*_deposit_only` 向量 ⇒ 收据 meta 覆盖为 8/9**，该后果要么在文件头注释写明「留给 Plan C」，要么由 B2 的逐档形状断言间接覆盖（二者必居其一，不得含糊）。
- [ ] **B3 有外部锚**：Step 1b 的 FIELDMAP 对账结论已记录；isthmus / jovian 两档的精确字段集按 FIELDMAP §4.2/§4.3 硬钉；`operator_fee*` 标注为"FISCO 派生、无 op-geth 背书"，`da_footprint` 标注载体为 `BlobGasUsed`（§5.2/§5.3）；**Ecotone 只验存在性**（值分叉见 FIELDMAP §6）写进测试头注释；operator 的发射规则按 §5.4（`scalar != 0 || constant != 0`）而非"档位有 operator_fee 就一定有"。
- [ ] **deposit 回执的形状已定性**：若 FISCO 的 deposit 回执不止两个 deposit 字段（FIELDMAP §4.1 的 op-geth 早退形态），该差异**已立案**（da-matrix `DIVERGENCES.md`）并在期望里注明来源。
- [ ] "fork × 负向面"覆盖表写进 `OpForkNegativeCoverageTest.cpp` 头注释，**含 SCOPE 段**（只扫 4 个面、错误码/latestValidHash 仅在 Isthmus/默认档实测、非逐 fork 叉乘），2 个 GAP 用例落位。
- [ ] ledger 套件 6 → 8 例全绿（9 档 + 重开、genesis 绑定负向）；9 档 canonical 的 keccak **硬编码**并双向断言（照既有 F12 写法）；keccak 改写负向沿用既有 `hashMismatchFailClosed`。
- [ ] **B6 四个变体全部 RED 且被归因**（`also_green` 保持 GREEN），跑完 `git status` 干净 —— 这是"判据有区分力"的证据，缺一个都视为未完成。
- [ ] 新文件都在 `SKIP_UNITY_BUILD_INCLUSION`；`cmake -B build -S .` 后确实被编译（不是空跑）。
- [ ] 每个 Task 一个 commit；**未推送**；`docs/**` 未进任何 commit；未使用目录级 `git add`。

## 4. 执行交接

计划已落盘 `docs/plans/2026-09-12-plan-B-impl.md`（untracked，按本仓约定不入库）。两种执行方式：

1. **Subagent-Driven（推荐）**：每个 Task 派新的 subagent 实施，任务间做两阶段审查（规格符合性 + 质量），迭代快。
2. **Inline Execution**：本会话内按 `executing-plans` 批量执行，到检查点停下复核。

选哪一种？
