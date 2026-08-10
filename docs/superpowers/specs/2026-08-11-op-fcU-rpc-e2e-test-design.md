# OP 模式 FCU 端到端测试 Design Spec

**日期**：2026-08-11
**状态**：设计定稿（/loop 审计发现缺口后产出）
**分支**：`feat-op-executor-e2e`
**来源**：/loop 审计——"从 RPC 入口正常执行 FCU 请求 + 覆盖完整测试"

## 1. 缺口（已核实）

用户要求"从 RPC 入口开始，能够正常执行 FCU 的请求"并覆盖端到端测试。现状：

| 层 | 现状 | 覆盖 |
|---|---|---|
| RPC 入口转发（`EngineEndpoint::forkchoiceUpdatedV1-V3` → `updateForkchoice`） | `EngineRpcTest.cpp:134/158/182` 有测试 | ✅ 但用 **mock EngineService**（`EngineRpcTest.cpp:69`），只验证参数转发，不验证真实语义 |
| **真实 `updateForkchoice` OP 语义**（`EngineServiceImpl.h:211-402`） | **零端到端测试**（opstack 测试无 FCU 引用） | 🔴 缺口 |

对比：`newPayload` 有 `OpNewPayloadRpcE2eTest`（真实 EngineService + OpScheduler + MLS，34 向量端到端）；**FCU 无对应真实端到端**。

## 2. 语义参照（op-geth v1.101702.2 `eth/catalyst/api.go:238-330`）

| op-geth forkchoiceUpdated 行为 | FISCO updateForkchoice 对应 |
|---|---|
| head hash 全零 → INVALID | （FISCO 需确认是否处理） |
| OP + attributes → `checkOptimismPayloadAttributes` 拒绝对（InvalidPayloadAttributes） | attributes → `-38003` `UnsupportedOpPayloadAttributes`（不打包） |
| head block 未知 → `STATUS_SYNCING`（网络拉取） | `getBlockNumber` 无值 → SYNCING（`EngineServiceImpl.h:253-262`） |
| head 已知 → `SetCanonical` + **VALID** + LatestValidHash=head（:316-322） | head 已知 → VALID + headBlockHash（`:334-338`） |
| safe/finalized 校验 | 单调性：safe/finalized ≤ head（`:263-280`，冲突抛 `InvalidForkchoiceState`） |

FISCO 与 op-geth 的 FCU 判定语义**对齐**（SYNCING/VALID 判定、attributes 拒绝），但缺测试证明。

## 3. 测试面（真实端到端）

新文件 `bcos-evm/test/opstack/OpForkchoiceRpcE2eTest.cpp`（复用 `OpNewPayloadRpcE2eTest` 的 fixture 模式：真实 EngineService + OpScheduler + MLS + 单桶 backend），经 RPC 层 `forkchoiceUpdatedV1-V3` 驱动真实 `updateForkchoice`：

1. **head 已知 → VALID**：先 newPayload 一个块（复用现有 golden 向量流程）→ FCU(head=该块) → VALID + LatestValidHash=head（对照 op-geth `valid()`）。
2. **head 未知 → SYNCING**：FCU(head=不存在 hash) → SYNCING（对照 op-geth `STATUS_SYNCING`）。
3. **attributes → -38003**：FCU(带 payloadAttributes) → JSON-RPC `-38003`（OP 拒绝构建，对照 op-geth attributes 拒绝）。
4. **单调性错误 → -38002**：FCU(finalized > head) → `InvalidForkchoiceState`（-38002）。
5. **head 递增**：FCU(head=块N) → FCU(head=块N+1) 成功；FCU(head=块N) 重复 → 冲突报错。
6. **无 attributes → head 推进**：FCU(无 attributes) → VALID + head 更新（getSafeBlockNumber/getFinalizedBlockNumber 反映）。

## 4. 实现要点

- 复用 `OpNewPayloadRpcE2eTest` 的 `OpE2eFixture`（MLS + 单桶 backend + OpEngineService）+ `GoldenSample`/`loadVectorSample`。
- RPC 入口：直接调 `OpEngineService::updateForkchoice`（真实语义）+ 或经 `EngineEndpoint::handleForkchoiceUpdated`（若 fixture 可搭 RPC 栈）——优先真实 RPC 层（对照 newPayload 的 onRPCRequest 模式）。
- **SYNCING 前提**：FCU 的 head 未知 → `getBlockNumber` 查 `SYS_HASH_2_NUMBER` 无值。需先确认测试环境该表为空（未 newPayload 的 head hash）。
- **单调性**：直接构造 `ForkchoiceState{finalized > head}` 断言抛 `InvalidForkchoiceState`。
- 断言对照 op-geth：VALID/LatestValidHash、SYNCING、-38003/-38002 错误码。

## 5. 边界（不做）

- **不实现** `forkchoiceUpdatedV4`（Amsterdam，用户裁定不处理）。
- **不实现** OP 出块（attributes → -38003 是本测试验证的语义，非缺陷）。
- **不补** `EngineRpcTest` 的 mock 测试（已覆盖 RPC 转发）。

## 6. 验收

1. 6 个测试面全绿（head 已知/未知/attributes/单调性/递增/无 attributes）。
2. 与 op-geth `forkchoiceUpdated` 判定语义对齐（对照 §2 表）。
3. opstack 全量回归不回归（153/153 + 新 FCU 用例）。
