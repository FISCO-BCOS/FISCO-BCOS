# W4：L0 静态对拍差异矩阵 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 产出 `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`——FISCO opstack ↔ op-geth v1.101702.2 块执行流程的完整差异矩阵（7 阶段 × 锚点三态判定 + B 项台账 + 结构性差异），并给 comparison doc §2 加指向。

**Architecture:** 纯静态文档任务。先产出「校正后锚点表」（W4 审查 R1/R2 证实 §1 锚点约半数需校正），再逐阶段对拍产矩阵行，最后差异点归位 + 汇总定稿。每行判定 = 三态（等价/已知分叉/结构性差异）+ file:line 证据 + 状态图例。

**Tech Stack:** Markdown 表格 + grep/Read 源码核验（本分支 + op-geth v1.101702.2 @ `~/octo/code/blockchain-impl/op-geth`）。

## Global Constraints

- **矩阵底稿 = 校正后锚点表，非 comparison doc §1 原文**（W4 审查 R1/R2 证实 §1 约半数 FISCO 锚点偏移/指向不存在文件、op-geth 有 1 处真错锚）
- 判定三态：`等价 / 已知分叉 / 结构性差异`；状态图例：`已确认 / 已修一致 / 事实达成 / 待W5`
- 单侧行（op-geth 独有、FISCO 无对应）→ FISCO 锚点列标「无对应 / 结构性差异」
- 差异点段落必须归位（映射到 B/D 项 / 矩阵行 / 显式标注），无孤儿
- op-geth 锚点一律以 `~/octo/code/blockchain-impl/op-geth`（v1.101702.2 @ e8800cff）为准
- commit `--no-verify`；所有命令在 worktree 内运行（勿 cd 主 checkout）
- `docs/opstack-opgeth-e2e-comparison.md` 在本 worktree 已有副本（含 §7）

---

### Task 1: 校正后锚点表（双端）

**Files:**
- Create: `bcos-evm/test/opstack/t8n/vectors/ANCHOR-CORRECTIONS.md`

**Interfaces:**
- Consumes: W4 审查 R1/R2 的校正清单（已给全，见下方各步）；comparison doc §1 锚点（原始参考）
- Produces: 校正后锚点表（Task 2-4 的矩阵底稿）

- [ ] **Step 1: 核对 FISCO 侧校正锚点**

对以下每个锚点，用 `grep -n` + Read 在**本分支**核实，确认真实存在且指向正确，写入 `ANCHOR-CORRECTIONS.md` 的 FISCO 表：

| 原锚点（§1/§0.0） | 校正为 | 核实命令 |
|---|---|---|
| `OpValidate.cpp:7`（文件不存在） | `OpTransition.cpp:328`（opValidate） | `grep -n "opValidate" bcos-evm/bcos-evm/opstack/OpTransition.cpp` |
| `OpDepositTx.cpp:58`（文件不存在） | `OpTransition.cpp:441`（runDeposit） | `grep -n "runDeposit" bcos-evm/bcos-evm/opstack/OpTransition.cpp` |
| `OpReceiptMap.h:120`（文件不存在） | `OpTransition.cpp:318-321`（makeFiscoReceipt/setOpStackMeta） | `grep -n "makeFiscoReceipt\|setOpStackMeta" bcos-evm/bcos-evm/opstack/OpTransition.cpp` |
| `OpBlockSeal.cpp:29` sealOpBlock | `:138` | `grep -n "sealOpBlock" bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp` |
| `OpBlockSeal.cpp:60-64` withdrawalsRoot | `:170-174`（调用点） | `grep -n "withdrawalsRoot" bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp` |
| `OpBlockSeal.cpp:74-80` blobGasUsed | `:187-197` | `grep -n "blobGasUsed\|da_footprint" bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp` |
| `OpBlockExecute.cpp:75` processOpBlock | `:99` | `grep -n "processOpBlock" bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp` |
| `OpBlockExecute.cpp:85-91` 首笔 deposit | `:112-116` | 同上文件读 :112-116 |
| `OpBlockExecute.cpp:115/:165` blockGasLeft | `:143/:198` | 同上文件读 :143/:198 |
| `OpBlockExecute.cpp:128` loadOpFeeParams | `:157` | `grep -n "loadOpFeeParams" bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp` |
| `OpBlockExecute.cpp:130-152` DA calldata | `:164-180`（JovianL1AttributesLen=178，提取在 :176-178） | 同上文件读 :164-180 |
| `OpTransition.cpp:143/:186-191` opTransition/vault | `:237`（opTransition）/`:295-300`（vault 路由） | `grep -n "opTransition\|OP_BASE_FEE_VAULT" bcos-evm/bcos-evm/opstack/OpTransition.cpp` |
| `OpSchedulerImpl.h:857` decodeOneRawTx | `:855` | `grep -n "decodeOneRawTx" bcos-evm/bcos-evm/engine/OpSchedulerImpl.h` |
| `EngineServiceImpl.cpp:233-239` | def `:191`；Jovian max 逻辑 :233-240 | `grep -n "calcOpBaseFee" engine/bcos-engine/EngineServiceImpl.cpp` |
| `StateRootCompute.h:83` | `:76` | `grep -n "stateRootOf" bcos-evm/bcos-evm/adapter/StateRootCompute.h` |
| `LedgerMethods.h:233-235` | `:237`（解引用） | `grep -n "txEntry" bcos-ledger/bcos-ledger/LedgerMethods.h` |
| `bcosTransactionToEvmone`（OP 锚点误） | OP 路径 = `OpTransition.cpp:456-457`（直接构造 evmone tx） | `grep -n "evmone::state::Transaction" bcos-evm/bcos-evm/opstack/OpTransition.cpp` |

同时记录 2 处实质过时（供矩阵差异点用）：缺口A 已修（`parseNewPayloadRequest` 填 rawTransactions :71-76 / withdrawalsRoot :116-119，`bcos-rpc/.../EngineHelper.cpp`）。

- [ ] **Step 2: 核对 op-geth 侧校正锚点**

在 `~/octo/code/blockchain-impl/op-geth` 核实：

| 原锚点 | 校正为 | 核实 |
|---|---|---|
| `core/blockchain.go:2086` ProcessBlock | **`:2121`**（:2086 是 `bpr.Witness()`） | `grep -n "func.*ProcessBlock" ~/octo/code/blockchain-impl/op-geth/core/blockchain.go` |
| `eip1559_optimism.go:22` | `consensus/misc/eip1559/eip1559_optimism.go:22` | 路径补全 |
| `rollup_cost.go` | `core/types/rollup_cost.go` | 路径补全 |
| `receipt.go:199` MakeReceipt | `state_processor.go:199` | `grep -n "func MakeReceipt" ~/octo/code/blockchain-impl/op-geth/core/state_processor.go` |

- [ ] **Step 3: 产校正后锚点表**

把 FISCO + op-geth 两张校正表写入 `ANCHOR-CORRECTIONS.md`，格式：

```
| 阶段 | 锚点 | 校正后 FISCO 锚点 | 校正后 op-geth 锚点 | 校正依据 |
```

依据列写「W4 审查 R1/R2 + 本次 grep 核实」。

- [ ] **Step 4: 自验 + Commit**

```bash
# 抽查 3 个 FISCO 校正锚点 + 1 个 op-geth 锚点能 grep 到
grep -n "opValidate" bcos-evm/bcos-evm/opstack/OpTransition.cpp
grep -n "sealOpBlock" bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp
grep -n "runDeposit" bcos-evm/bcos-evm/opstack/OpTransition.cpp
grep -n "func.*ProcessBlock" ~/octo/code/blockchain-impl/op-geth/core/blockchain.go
git add bcos-evm/test/opstack/t8n/vectors/ANCHOR-CORRECTIONS.md
git commit --no-verify -m "docs(w4): corrected anchor table (FISCO+op-geth) as DIVERGENCES.md base"
```

Expected: 4 条 grep 全命中；commit 成功。

---

### Task 2: 阶段 0-2 矩阵行

**Files:**
- Create: `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（建文件，写头 + 图例 + 阶段 0/1/2 表）
- Consumes: `ANCHOR-CORRECTIONS.md`（Task 1）

**Interfaces:**
- Consumes: Task 1 校正锚点表
- Produces: DIVERGENCES.md 的阶段 0/1/2 三张矩阵表

- [ ] **Step 1: 建 DIVERGENCES.md 头部 + 状态图例**

写文件头（标题、来源、口径）+ 状态图例（§4.1 四态）+ 判定三态说明。格式：

```markdown
# DIVERGENCES — FISCO opstack ↔ op-geth v1.101702.2 块执行差异矩阵

> 来源：W4（L0 静态对拍）。锚点以 `ANCHOR-CORRECTIONS.md` 校正后为准。
> 判定：等价 / 已知分叉 / 结构性差异。状态：已确认 / 已修一致 / 事实达成 / 待W5。
> 覆盖：7 阶段 + 8 项 B 项（B-5 拆 a/b/c）+ 结构性差异 + D 项。
```

- [ ] **Step 2: 阶段 0 矩阵（数据形态）**

用校正锚点写阶段 0 表（comparison doc §1 阶段0 的锚点对 + §0.0 C-8 修正）：

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| ExecutionPayload | `Types.h:89-130` | `types.go:252` DecodeTransactions | 等价 | 结构一致 | 已确认 |
| typed tx 解码 | `OpSchedulerImpl.h:855` decodeOneRawTx | `transaction.go:212` decodeTyped | 等价 | 分派覆盖 0x7E/0x01/0x02/0x04 | 已确认 |
| deposit 转换 | `OpTransition.cpp:456-457` | `deposit_tx.go:27-46` | 等价 | 字段映射一致 | 已确认 |
| 差异点：三次类型翻译 | — | — | 结构性差异 | FISCO 双端三次翻译 vs op-geth 一次 | 已确认 |

- [ ] **Step 3: 阶段 1 矩阵（RPC 入口）**

| 锚点 | FISCO | op-geth | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 注册 | `EndpointsMapping.cpp:68-71` | `api.go:695/703/724/743/770` | 结构性差异 | FISCO V4 桩 | 已确认 |
| 分派 | `EngineEndpoint.cpp:177-221` | `api.go:796` newPayload | 等价 | 唯一路径 | 已确认 |
| OP 校验 | `EngineServiceImpl.cpp:279/:299/:318` | `types.go:289/:320-327` | 等价 | raw/withdrawalsRoot 必填 | 已确认 |
| 差异点：校验位置 | validateOpNewPayloadRequest | checkOptimismPayload | 结构性差异 | 执行后 vs catalyst 层 | 已确认 |

- [ ] **Step 4: 阶段 2 矩阵（块校验）**

| 锚点 | FISCO | op-geth | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 块校验主体 | `EngineServiceImpl.h:1094-1147` 承诺比对 | `block_validator.go:51` ValidateBody | 结构性差异 | 执行后 vs 预校验 | 已确认 |
| extraData | 无对应 | `eip1559_optimism.go:22` | 结构性差异 | FISCO 无独立 | 待W5 |
| base fee | `calcOpBaseFee` def `EngineServiceImpl.cpp:191` | `eip1559.go:64/:99-107` | 等价 | Jovian max 一致 | 事实达成 |
| DA footprint ==≤GasLimit | `EngineServiceImpl.cpp:442-444` | `block_validator.go:119-134` | 等价 | 均实现 | 事实达成 |
| 单侧：DA 拒绝路径 | 无对应（未触发） | `block_validator.go:131-132` | 结构性差异 | 无拒绝用例 | 待W5 |

> ⚠️ base fee 的 FISCO 锚点是 `EngineServiceImpl.cpp:191`（def）——§1 写 :233-239 是 Jovian max 逻辑，两者都要在证据列区分。

- [ ] **Step 5: 自验 + Commit**

```bash
# 抽查 3 个阶段 0-2 锚点在双端存在
grep -n "ExecutionPayload" bcos-framework/bcos-framework/engine/Types.h
grep -n "func.*newPayload" ~/octo/code/blockchain-impl/op-geth/eth/catalyst/api.go
git add bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md
git commit --no-verify -m "docs(w4): DIVERGENCES stage 0-2 matrix (data form / RPC entry / block validation)"
```

Expected: grep 命中；commit 成功。

---

### Task 3: 阶段 3-4 矩阵行（核心）

**Files:**
- Modify: `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（追加阶段 3/4 表）
- Consumes: Task 1 校正锚点表

**Interfaces:**
- Consumes: Task 1 校正锚点表
- Produces: DIVERGENCES.md 的阶段 3/4 两张矩阵表（核心）

- [ ] **Step 1: 阶段 3 矩阵（状态执行）**

| 锚点 | FISCO | op-geth | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 块执行入口 | `OpBlockExecute.cpp:99` processOpBlock | `state_processor.go:62` Process | 等价 | 首笔 deposit 强制（:112-116） | 已确认 |
| 首笔 L1 attributes | `OpBlockExecute.cpp:112-116` | `state_transition.go:346-361` preCheck | 等价 | D-1 交易级 | 已确认 |
| blockGasLeft 递减 | `OpBlockExecute.cpp:143/:198` | `state_transition.go:282` buyGas | 等价 | B-4 相关 | 事实达成 |
| fee 惰性加载 | `OpBlockExecute.cpp:157` loadOpFeeParams | `rollup_cost.go:151/215/353` | 等价 | D-4 相关 | 已确认 |
| validate | `OpTransition.cpp:328` opValidate | `state_transition.go:346` preCheck | 等价 | D-1 | 已确认 |
| fee 路由至 vaults | `OpTransition.cpp:295-300` | `state_transition.go:711-734` | 等价 | C-5 校正后 | 已确认 |
| deposit 执行 | `OpTransition.cpp:441` runDeposit | `state_transition.go:473-511` | 等价 | D-1 | 已确认 |
| 差异点#1 快照契约 | OpValidate/opTransition 共享快照 | buyGas/innerExecute 即时读 | 已知分叉 | D-4 | 待W5 |

- [ ] **Step 2: 阶段 4 矩阵（块级收尾）**

| 锚点 | FISCO | op-geth | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| txRoot | `OpEngineSeam.h:171` computeOpTxRoot | `block.go` DeriveSha | 等价 | HashBuilder 排序修复 | 已修一致 |
| 密封 header | `OpBlockSeal.cpp:138` sealOpBlock | `consensus.go:383` FinalizeAndAssemble | 等价 | encodeOpHeader 字节级 | 事实达成 |
| withdrawalsRoot | `OpBlockSeal.cpp:170-174` | `consensus.go:416-427` | 等价 | B-1 | 已修一致 |
| blobGasUsed | `OpBlockSeal.cpp:187-197` | `consensus.go:429-437` | 等价 | B-5a | 事实达成 |
| stateRoot | `StateRootCompute.h:76` stateRootOf | `blockchain.go:1681-1697` Commit | 等价 | 33 向量 | 事实达成 |
| 回执映射 | `OpTransition.cpp:318-321` | `state_processor.go:199` MakeReceipt | 等价 | B-2 | 事实达成 |
| 回执扩展字段 | `OpTransition.cpp:319` setOpStackMeta | `receipt.go:596` DeriveFields | 已知分叉 | B-3 | 已确认 |

- [ ] **Step 3: 自验 + Commit**

```bash
grep -n "computeOpTxRoot" bcos-evm/bcos-evm/engine/OpEngineSeam.h
grep -n "func MakeReceipt" ~/octo/code/blockchain-impl/op-geth/core/state_processor.go
git add bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md
git commit --no-verify -m "docs(w4): DIVERGENCES stage 3-4 matrix (state execution / block sealing)"
```

Expected: 命中 + commit 成功。

---

### Task 4: 阶段 5-6 + 结构性差异 + D 项 + 差异点归位 + B 台账

**Files:**
- Modify: `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（追加阶段 5/6 + 结构性 + D 项 + B 台账 + 差异点归位）
- Consumes: Task 1 校正锚点表、comparison doc §2/§7

**Interfaces:**
- Consumes: Task 1-3
- Produces: DIVERGENCES.md 全部剩余章节

- [ ] **Step 1: 阶段 5-6 矩阵**

阶段 5（落库）：`registerOpBlock`（EngineServiceImpl.h:1191；SYS_NUMBER_2_HASH :1198、SYS_HASH_2_NUMBER :1204、SYS_NUMBER_2_BLOCK_HEADER :1218、SYS_HASH_2_RECEIPT :1287、SYS_ETH_HASH_2_RAWTX :1301）vs `writeBlockWithState`（blockchain.go:1650/:1664-1665）——判定：结构性差异（FISCO 故意不写 SYS_HASH_2_TX，:1226-1253 论证）→ 已确认。附 `LedgerMethods.h:237` UB 注。

阶段 6（输出）：block hash 承诺比对（`EngineServiceImpl.h:799-803` + :1094-1147）vs `Header.Hash()`（block.go:124）——判定：等价 → 已确认。output root 不在 EL 范围 → 标注。

- [ ] **Step 2: 结构性差异 + D 项**

结构性差异（4 项，来自 §2 结构性差异节，用校正锚点）：
- 块校验位置（承诺比对 vs ValidateBody）
- 双执行器并存（`OpstackExecutor.h` vs executeOpBlock 单路径）
- 索引隔离（SYS_HASH_2_TX 不写）
- PBFT 双执行防护（`OpSchedulerImpl.h:987/:993` throw 哑桩）

D 项（§2 已确认，直接纳入）：D-1 交易级逐位等价 / D-2 Karst 占位 🔴 / D-3 DA footprint 不进 tx 级 / D-4 费用快照契约 ⚠️

- [ ] **Step 3: 差异点归位**

按 §4.2 归位规则，把 comparison doc §1 每阶段「差异点」段落逐一映射，确认无孤儿：
- 阶段0 三次类型翻译 → 已入 Task 2 阶段 0 表（结构性差异）
- 阶段1 校验位置 → 已入 Task 2 阶段 1 表（结构性差异）
- 阶段3 #1 快照契约 → 已入 Task 3（D-4）
- 阶段3 #2 fee 惰性加载 → 已入 Task 3（等价，已确认）
- 阶段3 #3 DA 不进 tx 级 → D-3
- 阶段4 #1/#2/#3 → B-1/B-2/B-3
- 阶段5 → 结构性差异（索引隔离）
- 阶段6 → 不在范围（output root 属 op-node）

写一节「差异点归位对照表」列全部映射，每个指向 B/D/矩阵行/标注。

- [ ] **Step 4: B 项台账**

按 spec §4.3（含 B-5a/b/c 拆分 + B-3/B-7 细化，4-agent 审查后）：

| B 项 | 判定 | 状态 |
|---|---|---|
| B-1 | 等价 | 已修一致 |
| B-2 | 等价 | 事实达成 |
| B-3 | 已知分叉（2 delta） | 已确认 |
| B-4 | 等价 | 事实达成 |
| B-5a | 等价 | 事实达成 |
| B-5b | 结构性差异-待验 | 待W5 |
| B-5c | 等价-待验 | 待W5 |
| B-6 | 等价 | 已确认 |
| B-7 | 等价（效果已证） | 待W5（顺序可观测性） |
| B-8 | 等价 | 已修一致 |

每行加依据列（锚点 + 一条证据）。

- [ ] **Step 5: 自验 + Commit**

```bash
grep -n "registerOpBlock" engine/bcos-engine/EngineServiceImpl.h
grep -n "func.*writeBlockWithState" ~/octo/code/blockchain-impl/op-geth/core/blockchain.go
git add bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md
git commit --no-verify -m "docs(w4): DIVERGENCES stage 5-6 + structural + D items + diff-point mapping + B ledger"
```

Expected: 命中 + commit 成功。

---

### Task 5: DIVERGENCES.md 定稿 + comparison doc §2 指向

**Files:**
- Modify: `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（一致性终检）
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（§2 头部加指向 + B 项状态摘要表）

**Interfaces:**
- Consumes: Task 1-4 产物
- Produces: 最终 DIVERGENCES.md + §2 指向

- [ ] **Step 1: DIVERGENCES.md 一致性终检**

逐节检查：7 阶段全有表、每行三态 + 证据 + 状态齐全、B 台账 10 行（B-5 拆 3）、无「未判」残留（待W5 只作状态）、单侧行有「无对应」约定。缺则补。

- [ ] **Step 2: comparison doc §2 加指向**

在 `docs/opstack-opgeth-e2e-comparison.md` §2 标题下加：

```markdown
> **完整差异矩阵见** `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（W4，L0 静态对拍）。
> **B 项最新状态**：B-1 已修一致 / B-2 事实达成 / B-3 已确认 / B-4 事实达成 / B-5a 事实达成 / B-5b 待W5 / B-5c 待W5 / B-6 已确认 / B-7 待W5（顺序可观测性）/ B-8 已修一致
```

- [ ] **Step 3: 自验 + Commit**

```bash
# 终检：DIVERGENCES 章节数 + B 台账行数
grep -c "^## " bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md   # 应 >= 10
grep -c "^| B-" bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md   # 应 == 10
git add bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w4): finalize DIVERGENCES.md + comparison doc §2 pointer + B-item summary"
```

Expected: 章节数 >= 10、B 台账行 == 10；commit 成功。

---

## 执行顺序与验收

```
Task 1 (校正锚点表) → Task 2 (阶段0-2) → Task 3 (阶段3-4) → Task 4 (阶段5-6+B台账) → Task 5 (定稿+指向)
```

- 每任务独立可审：Task 1 靠 grep 双端锚点；Task 2-4 靠矩阵行完整性 + 锚点命中；Task 5 靠章节/行计数
- 验收（spec §6）：7 阶段全矩阵 + B 台账 10 行 + 结构性 + D 项 + 差异点全归位 + 单侧行约定 + §2 指向 + 多 agent 审查通过
- 审查：SDD task-reviewer 逐任务 + 最终 whole-branch review（opus）复核每行判定的证据链
