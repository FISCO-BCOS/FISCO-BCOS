# Plan B 设计文档 —— 夹具层（2026-09-12）

> 层级：本文是 **设计层**（架构、决策、边界、契约）。步骤级实施计划由 writing-plans 另出，
> 落盘 `docs/plans/2026-09-12-plan-B-impl.md`。
> 规格来源：`docs/plans/2026-09-12-plan-B-fixture-layer.md`（576 行，含 B3/B4/B5 的"补齐"节）；
> 工作项索引：`docs/plans/2026-09-12-opstack-fork-test-workitems.md`（WI-15/16/17/18/20）。
> 过程文档：**untracked，不入库**（`docs/**` 永不提交）。

## 0. 已核准的决策与对规格的修正

**决策（本会话与用户确认）**

| # | 决策 | 取值 |
|---|---|---|
| D1 | 阶梯遇到某档不通过时的交付形态 | **单用例 × 9 档、红了就停**（报出档位/期望/实测；不改 rung 表；按 M0 台账或新 finding 定夺） |
| D2 | 自定义 schedule 注入夹具的方式 | **A1：tag + canonical 字符串构造**，在成员初始化列表里构造 `seamScheduler` |

**对规格的修正（以"补齐"节为准，本文不复述被取代的原文）**

| 规格原文 | 修正后 | 原因 |
|---|---|---|
| B3：Create `engine/test/.../OpForkNegativeCoverageTest.cpp` | **Modify** `opstack-executor/tests/OpT8nReplayTest.cpp` | `loadBlockContext`/`replaySingleBlockInto`/`DivergenceLedger` 都是该文件的**文件内静态**函数，新建文件复用不了 |
| B5：Create `engine/test/.../OpForkSchedulePersistenceTest.cpp` | **Modify** `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp` | 该文件已有 `makeL2GenesisTestStorage()`/`scheduleGenesis()` 与真实 round-trip（`:98`） |
| B5：Step 1 "定位读写入口" | 已定位，见 §8 | codec/3-key/读写/启动 resolve 全部已核 |

因此 Plan B 的文件面全部落在**测试侧**：`engine/test/**`、`opstack-executor/tests/**`、`bcos-ledger/test/**`。**生产代码零改动**；只用生产公开接口（`OpForkSchedule::parse` 为 public，`OpForkSchedule.h:109`；seam 构造见 `OpSchedulerSeam.h:71`）。

## 1. 目标 / 非目标

**Goal**：让引擎夹具能表达"一条跨全部 9 个 fork 的连续链"，并补齐只能靠**真实执行路径**才能断言的格子——逐 fork 收据 meta 形状、F8 负向格、fork 调度跨重开的持久化。

**非目标**（YAGNI）：
- 不做执行一致对拍（需要 op-geth 产链，属 Plan D）；B 只保证"自己这条链在 9 档上自洽"。
- 不改生产代码。若某档失败指向生产缺陷 → 记 finding，另行授权再修（同 Plan A 的纪律）。
- 不重复既有覆盖：B4 只补实测出来的 GAP；B3 不再造已覆盖的字段断言。

## 2. 前提复核（实测，取代规格 §0.2 的书面断言）

| # | 前提 | 结论 | 证据 |
|---|---|---|---|
| 1 | 只能在成员初始化列表里换 schedule | ✅ 成立 | `seamScheduler` 是成员默认初始化（harness `:1177-1179`）；`OpSchedulerSeam` 删除拷贝/移动（`OpSchedulerSeam.h:189-192`）；`OpEngineService` 以 `SchedulerType&` 持有（`OpEngineService.h:151/157/350`） |
| 2 | 请求构造器硬编码 | ✅ 成立，且**面更宽** | `validRequest` 钉 `has_da_footprint=false`（harness `:1277`、`:1287`）；**新发现**：`makeValidIsthmusNewPayload`（`:627-655`）自己在 `:652` 把 `OpForkId::Isthmus` 钉进 header rebuild，payload 也是 Isthmus 形状（9 字节 extraData）；而 harness `:495` **已经**从 schedule 派生 `has_da_footprint` → 参数化应"问 schedule"，不是继续加布尔参数 |
| 3 | 早期 fork 形状不同，手搓 payload 不可复用 | ✅ 成立 | `makeRegolithAttrs`/`makeCanyonAttrs`/`makeEcotoneAttrs` 的构造链（harness `:776-802`）显式剥掉 pre-Holocene 字段 |
| 4 | 有现成 attrs 工厂与构建驱动 | ✅ 成立 | `makeOpPayloadAttributesAt`（`:769`）、`buildPayloadAt`（`:807-825`，且已注明"强制 tx 用 deposits-only，否则 Jovian/Karst 激活窗是 FCU-INVALID"） |
| 5 | RPC 层不看 fork | ✅ 成立 | `ReceiptResponse.cpp:100-140` 逐字段只判 `optional` 是否存在（连 Bedrock 期 `l1FeeScalar` 都是"存在即上游 fork 门"的注释） |
| 6 | `op_fork_schedule` 持久化零覆盖 | ✅ 成立 | codec 有独立单测（`bcos-evm/test/opstack/OpForkScheduleCodecTest.cpp`），但**读写/重开/绑定**路径无覆盖；入口见 §8 |

## 3. 架构

三块改动，逐块独立可验；一个注入点贯穿：

```
B1 夹具注入 ──┬─> B2 阶梯（1 用例 × 9 档；服务自产 payload）
              ├─> B3 逐 fork 收据 meta（执行路径断言）
              ├─> B4 F8 负向格（2 个 GAP）
              └─> B5 调度持久化（写→重开→读回 + 2 负向）
```

数据流（B2 每档）：`fixture(schedule) → service.updateForkchoice(attrs@t) → getPayload(payloadId) → newPayload(payload)`。
断言层（B3/B4）：`cfg（OpForkConfig）` 为 oracle，被测代码只提供"实际值"——**不读被测代码来构造期望**。

## 4. B1 夹具注入与请求参数化（WI-15）

**注入（D2 = A1）**：新增 tag 构造，沿用既有 tag 分发惯例（夹具已有 `DelegateFromFactory`、`BlockingGate` 两个 tag 构造）：

```cpp
struct WithSchedule {};  // 语义：用 canonical 文本构造 schedule，走生产解析器

explicit ImportServiceFixtureT(WithSchedule, std::string_view canonical, int stripImportDeltaAt = -1)
  : seamScheduler(std::make_shared<bcos::evm::opstack::OpForkSchedule>(
        bcos::evm::opstack::OpForkSchedule::parse(canonical)), {}),
    delegate(makeImportDelegate<StorageType>(stripImportDeltaAt, blockFactory, storage, ioServicePool)),
    service(memPool, storage, seamScheduler, blockFactory,
        bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
{
    seedGenesisAndForkchoice();
}
```

契约：
- 现有构造**一字不改**，`seamScheduler` 的默认成员初始化仍是 `legacy(false)` → 既有 engine 用例零影响（**Plan A 结束时 `test-bcos-engine` = 344**；规格文中的"338"是 Plan A 落地前的旧数，B1 的计数锚用 344）。
- schedule 用 `OpForkSchedule::parse`，即 **ledger 的 canonical 格式**：顺带验证生产格式（0 基线 + fork 严格连续）。
- 需要畸形 schedule 的场景另加 tag（走 `OpForkSchedule` 的 `TestBypass` 构造）；**B2/B3/B4 不需要**，B5 走 ledger 侧不经过夹具。

**参数化**（把"硬编码"换成"问 schedule"）：
- 新增 `makeNewPayloadAt(OpForkId forkId, bool hasDaFootprint, ...)`；`makeValidIsthmusNewPayload` 保留为薄包装（`forkId = Isthmus, hasDaFootprint = false`），`validRequest` 同样保留 → 既有调用点不动。
- 派生值来源统一为 `schedule.configAt(ts)` / `forkAt(ts)`（seam 已暴露，harness `:495` 已是这个写法），不在夹具里再复制一份 fork→形状 的映射。

**风险**：改的是共享头 → 开工前先跑一次全量计数并把绝对值记进本轮（B1 验收要求"与 Plan A 结束时一致"，需要具体数字做锚）。

## 5. B2 全 fork 阶梯（WI-16）

**形态（D1）**：`OpEngineForkLadderSuite` 1 用例 × 9 档；失败**停在该档**并报 `档位 / 期望 / 实测`；**不得为变绿改 rung 表**。

**rung 表**（canonical 文本 + attrs 工厂）：

```
0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,5000:holocene,6000:isthmus,7000:jovian,8000:karst
```

| rung | 激活 ts(s) | attrs 工厂 | 依据 |
|---|---|---|---|
| Regolith | 0 | `makeRegolithAttrs` | 无 withdrawals/beaconRoot/eip1559Params/minBaseFee |
| Canyon | 1000 | `makeCanyonAttrs` | +withdrawals |
| Ecotone | 2000 | `makeEcotoneAttrs` | +beaconRoot |
| Fjord | 3000 | `makeEcotoneAttrs`（复用） | Fjord/Granite 无 Holocene 字段 |
| Granite | 4000 | `makeEcotoneAttrs`（复用） | 同上 |
| Holocene | 5000 | `makeOpPayloadAttributesAt` | 含 eip1559Params + minBaseFee |
| Isthmus | 6000 | `makeOpPayloadAttributesAt` | 同上 |
| Jovian | 7000 | `makeOpPayloadAttributesAt` | 同上 + `has_da_footprint` |
| Karst | 8000 | `makeOpPayloadAttributesAt` | 同上 |

> **Fjord/Granite 复用 Ecotone attrs 是假设，首跑验证**：`makeOpPayloadAttributesAt` 带 `eip1559Params`/`minBaseFee`（Holocene+），在 Fjord/Granite 上会让 FCU 返回 Invalid 而不抛异常（harness `:776-779` 的注释就是这个坑）。

**每档步骤**：`buildPayloadAt(pair, ts*1000, parentTs*1000)` → `getPayload(payloadId, version)` → `newPayload(request, version)`；断言 `Valid`，并断言该档 `forkAt(ts)` 等于 rung 的 fork（证明档位真的切过去了，不是同一档跑 9 遍）。方法版本取 `engineApiProfileFor`（不得硬编码）。
> 期望该档 fork 的来源：测试**自己**对同一 canonical 文本调 `OpForkSchedule::parse(canonical).forkAt(ts)`（`OpForkSchedule.h:117` 为 public）——独立于服务的内部状态。注意 `OpSchedulerSeam` 只公开 `configAt`（`OpSchedulerSeam.h:127`），**不公开 `forkAt`**。
**Jovian/Karst 档**只能强制 deposit（激活窗 deposits-only，Q5）——`buildPayloadAt:807-825` 已按此实现，直接复用。
**计格**：一个文件内计数器累加实际跑过的档数，用例末尾断言 `>= 9`，防"0 档假绿"。

## 6. B3 逐 fork 收据 meta 形状（WI-17）

**位置**：`opstack-executor/tests/OpT8nReplayTest.cpp`（既有文件 → 不改 CMake）；target `opstack-executor-block-tests`。
> ⚠️ 该 target 的源文件是**显式列表**（`opstack-executor/tests/CMakeLists.txt:146-161`，无 GLOB）：本 Task 不动它，但**以后新建 `.cpp` 必须手改那一行**——与 engine target 的 GLOB 是相反的陷阱。

**oracle**：由 `OpForkConfig` 派生（fee 臂 → Bedrock/Ecotone 字段集；`has_operator_fee` → operator 三字段；`has_da_footprint` → DA 两字段；存款回执 + `deposit_nonce`，Regolith 例外无 `deposit_receipt_version`）。
**断言**：`actualMetaFields(receipt)`（读 `opStackMeta()` 的 14 个 optional）与期望集**双向**比对——缺字段红、多字段也红（防"顺手多填一个"）。

**覆盖腿**（实测，来自规格"补齐"节）：
- `*_deposit_only`：**8 档**（Regolith…Jovian，**无 Karst**）→ 主腿。
- `*_transfer_basic`：**7 档**（**无 Granite**）→ 对具备该向量的档另加。
- Karst 无对应向量 → **显式记为覆盖缺口**（补 Karst 向量属 Plan C/语料，不在 B 里造）。

**偏离依赖**：Regolith 存款回执不带 `deposit_receipt_version`（op-geth 从 Canyon 起写）。执行时**先核** `vectors/DIVERGENCES.md` 是否已登记该豁免：登记过就在期望函数里注明来源，未登记即 finding。

## 7. B4 F8 负向格（WI-18）

**先盘点、后补格**：覆盖表（实测，含 file:line）已在规格"补齐"节给出，四类面里只剩 **2 个 GAP**：

1. **OP 侧 stale head 的 FCU 语义**（Eth 侧有 `EngineServiceTest.cpp:677`，OP 侧无）。
2. **OP 侧多字段首错顺序**（实现顺序契约在 `OpEngineService.cpp:337-361`：transactions → withdrawals → blobVersionedHashes → windowFields → headerFields → blobGasUsed，且 window 排在 header 之前；Eth 侧有 `EthEngineServiceParityTest.cpp:949`，OP 侧每条只 mutate 一个字段）。

**位置**：`engine/test/unittests/engine/OpForkNegativeCoverageTest.cpp`（新建；engine target 是 `GLOB_RECURSE` → 必须 `cmake -B build -S .` 重配，否则 suite 根本不编译）。
**断言**：给该 fork 的 `configAt` 构造越界 payload/attrs，断言**具体状态或消息子串**（不用"抛异常"）；首错顺序那条要精确等于上面那条顺序所决定的那一条。
**验收物**：那张"fork × 负向面"覆盖表写进**测试文件头注释**（不只留在过程文档里）。

## 8. B5 调度持久化（WI-20）

**位置**：`bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp`（既有；复用 `makeL2GenesisTestStorage()`/`scheduleGenesis()` 与 `:98` 的 round-trip 形态）；target `test-bcos-ledger`。**不改 CMake**。

**入口（已核，无需再 grep）**：
- codec：`parseOpForkSchedule(canonical)`（`OpForkScheduleCodec.h:196`）、`canonicalOpForkSchedule(span)`（`:233`）、`keccakOpForkScheduleHash(canonical)`（`:239`）。**没有 `encode`/`decode` 这两个名字。**
- 3 个 key：`op_fork_schedule` / `op_fork_schedule_hash` / `op_fork_schedule_genesis`（`ChainMetadata.h:38-40`），挂在 `SYS_CHAIN_METADATA`，拼 key 用 `opForkScheduleMetadataKey`（`:169-172`）。
- 写：`writeOpForkScheduleMetadata(storage, metadata)`（`:211-229`）；唯一生产调用者是创世写（`bcos-ledger/.../Ledger.cpp:2382-2386`）。
- 读：`readOpForkScheduleMetadata(storage, expectedGenesisHash)`（`:200`）→ 校验 genesis 绑定与 keccak（`:101-124`）。
- 启动 resolve：`resolveOpForkScheduleCanonical(...)`（`:129`），生产调用 `libinitializer/Initializer.cpp:550-554`。

**三条用例**：
1. **9 档写入 → 重开 → 逐档一致**：用规格给出的 canonical 文本；"重开"= 在**同一个 backing storage** 上新建第二条读取路径（照 harness `:727-750` 的 `BackendMemStorage → CheckpointBackend → MLS` 样板）；断言读回的 `forkAt(t)`/`configAt(t)` 在 9 条边界上与写入前逐档一致。
2. **genesis 绑定不匹配必须 fail-closed**：用另一个 genesisHash 读 → `BOOST_CHECK_THROW(..., InvalidOpForkSchedule)` 且 `what()` 含 `"op fork schedule genesis binding mismatch"`。
3. **keccak 被改写必须 fail-closed**：写完直接覆盖 `op_fork_schedule_hash` 那一行 → 消息含 `"op fork schedule hash mismatch"`。

**若②③在生产里没有守卫** → 记 finding，**不在 B 里补生产**。

## 9. 文件面与构建契约（全部测试侧）

| 文件 | 动作 | target | CMake / 构建注意 |
|---|---|---|---|
| `engine/test/unittests/engine/support/OpEngineKarstTestHarness.h` | Modify | `test-bcos-engine` | 共享头：改前后各跑一次全量计数做锚 |
| `engine/test/unittests/engine/OpEngineForkLadderTest.cpp` | Create | `test-bcos-engine` | engine 是 `GLOB_RECURSE` → **必须** `cmake -B build -S .` |
| `engine/test/unittests/engine/OpForkNegativeCoverageTest.cpp` | Create | `test-bcos-engine` | 同上 |
| `opstack-executor/tests/OpT8nReplayTest.cpp` | Modify | `opstack-executor-block-tests` | 显式源列表，本 Task 不改它 |
| `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp` | Modify | `test-bcos-ledger` | 无需改 CMake |

新文件一律加 `SKIP_UNITY_BUILD_INCLUSION`（用到共享夹具的重头文件）。计数口径 `Running N test cases`；**判红不得用** `errors detected` 子串；套件名写错 = 0 例假绿。

## 10. 验收（计划 7 条 + 本文补充）

计划原 7 条（自定义 schedule 实际被用且既有 engine 用例不回归 / 阶梯 1×9 全绿或有台账记录 / meta 双向断言 / 覆盖表入注释 + GAP 已补 / 重开 + 2 负向 / 新文件进 SKIP_UNITY 且真被编译 / 过程文档未入库），**另加**：

- 改共享头**前后**的全量计数都要报数（B1 需要绝对值锚）。
- B3 的 Karst 覆盖缺口要显式登记（8/9 档），不得默认"9 档都验了"。
- B5 的两条负向若在生产无守卫 → 必须以 finding 记录并停（不得在 B 内改生产）。
- 每个 Task 一个 commit，消息按规格给出的文案；**不推送**；`docs/**` 不进任何 commit。

## 11. 风险与开放问题

| # | 风险 / 未知 | 处置 |
|---|---|---|
| 1 | Fjord/Granite 复用 Ecotone attrs 是**假设** | 首跑即验；若 Invalid，先查 M0 台账再改 rung 映射（改映射 ≠ 改期望，但要记录依据） |
| 2 | 阶梯的时间戳约定与既有 `c_karstPayloadTimestampMs = 1'000'000`（=1000 s）不一致 | 阶梯用**自己的** 9 档 canonical（0…8000 s）；既有单 fork 夹具保持 1000 s 不动。两者不共用，需在测试头注释里写明 |
| 3 | Karst 无 `*_deposit_only`/`*_transfer_basic` 向量 | meta 覆盖记为 8/9；补向量属 Plan C（需语料授权） |
| 4 | 阶梯能否在 Karst 档构建出合法块（deposits-only 约束） | 首跑验证；`buildPayloadAt` 已按 deposits-only 实现 |
| 5 | 改共享头的连带回归 | 串行构建；改前跑全量计数；失败先查夹具而非改断言 |

## 12. WI 映射

| WI | Task | 交付 |
|---|---|---|
| WI-15 | B1 | fixture tag 构造 + 请求参数化（薄包装保既有调用点） |
| WI-16 | B2 | `OpEngineForkLadderSuite` 1×9 |
| WI-17 | B3 | 逐 fork 收据 meta 双向断言 + 计数器 |
| WI-18 | B4 | "fork × 负向面"表（入注释）+ 2 个 GAP 用例 |
| WI-20 | B5 | 9 档写→重开→读回 + genesis 绑定 / keccak 两个负向 |
