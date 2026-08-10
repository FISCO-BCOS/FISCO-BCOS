# W7：结论定稿 + Karst 上线闸 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `docs/opstack-opgeth-e2e-comparison.md` 追加 §8 结论定稿 + Karst 上线闸——7 阶段三态判定 + 差异落定 + gap 清单 + Go/No-Go，经多 agent 证据级审查。

**Architecture:** 纯文档任务，分 5 个 task 起草 §8 各节（§8.1+8.2 判定 / §8.3 差异 / §8.4 上线闸 / §8.5+定稿），最后多 agent 证据级审查（对齐 comparison doc §0.0 先例）审计判定 vs 三输入并修复。

**Tech Stack:** Markdown + grep/Read 证据核对（DIVERGENCES.md / W5 测试 / W6 §7）。

## Global Constraints

- **产出** = `docs/opstack-opgeth-e2e-comparison.md` 追加 `## §8 结论定稿 + Karst 上线闸（W7，2026-08-07）`（标题对齐 §7 格式）
- **判定三态**：等价 / 已知分叉 / 结构性差异；**状态**：已确认 / 已修一致 / 事实达成（含已确认内部状态区分）
- **阶段聚合修正（W7 审查 R1/R3）**：阶段 1 = 等价 + **2 结构性**（注册 V4桩/V5缺 + 校验位置）；阶段 2 = 等价 + **2 结构性**（承诺比对 + DA 拒绝路径 B-5b）；阶段 3/4 = 等价 + **1 已知分叉**（D-4 / B-3）
- **桥接说明（R3）**：「逐位一致」限定于块级共识字节（stateRoot/receiptsRoot/withdrawalsRoot/encodeOpHeader），D-4/B-3 分叉不进块级字节
- **索引隔离接受注记（R2/R3）**：SYS_HASH_2_TX 刻意不写 → **`eth_getTransactionReceipt` 恒 null**（当前分支未合 rawtx 回退）——功能性 RPC 缺口，入 gap 清单
- **D-2 阻塞论证（R2）**：无 karstTime 激活通道 + karstConfig 死代码 + 前向兼容风险（非「别名≠真实 Karst」——op-geth v1.101702.2 的 Karst 是纯 config fork）
- **Go 条件含「重新对拍」**（Karst 专项金标准，仿 B-5c 链式对）
- **B-2/B-4 正式迁移 + B-3 注记收紧 = W7 内完成**（§8.3 落定，不入 §8.5 移交）
- **§8.5 移交**：Karst 适配专项 / OP 回执可查修复 / PBFT 共识决策 / deferred minors / 生产互通待办（指针 = comparison doc §7「W6 外待办」，非 §7.3）/ **W8/W0**
- 计数口径用可追溯值（W6 §7「OP 145/145」+ W5 新增 4 用例 ≈ 149，注明推导）
- commit `--no-verify`；worktree 内运行

---

### Task 1: §8.1 总体结论 + §8.2 7 阶段判定表

**Files:**
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（追加 §8 头部 + §8.1 + §8.2）

**Interfaces:**
- Consumes: DIVERGENCES.md 阶段 0-6 矩阵；spec §4 §8.1/§8.2
- Produces: §8 标题 + §8.1 结论 + §8.2 判定表（Task 2-4 在其后追加）

- [ ] **Step 1: 读三输入，核对阶段判定**

读 `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md` 阶段 0-6 矩阵 + B 台账 + D 项 + M-B 台账；`docs/opstack-opgeth-e2e-comparison.md` §7（W6 报告）。按 Global Constraints 的阶段聚合修正核对每个阶段的判定。

- [ ] **Step 2: 写 §8 头部 + §8.1 + §8.2**

在 `docs/opstack-opgeth-e2e-comparison.md` 末尾追加：

```markdown
## §8 结论定稿 + Karst 上线闸（W7，2026-08-07）

> **形态**：三层对拍（W4 L0 静态矩阵 + W5 L1 动态 gate + W6 L2 e2e）汇总成最终裁决。
> **范围**：EL 执行器等价 + Karst 就绪；生产互通待办见 §8.5。

### 总体结论

FISCO OP 执行器**核心执行路径（阶段 0/3/4）与 op-geth v1.101702.2 逐位一致**（限定于块级共识字节——stateRoot/receiptsRoot/withdrawalsRoot/encodeOpHeader；D-4 快照时序契约与 B-3 RPC 扩展字段是矩阵级「已知分叉」，不进块级字节）。差异集中在**结构层设计（索引隔离/块校验位置/双执行器）**与 **Karst 未适配（D-2 🔴）**。

### 7 阶段三态判定表

| 阶段 | 聚合判定 | 依据（DIVERGENCES 矩阵行） |
|---|---|---|
| 0 数据形态 | 等价 + 1 结构性 | 三次类型翻译（结构性，已确认） |
| 1 RPC 入口 | 等价 + 2 结构性 | 注册（V4 桩/V5 缺失）+ 差异点：校验位置 |
| 2 块校验 | 等价 + 2 结构性 | 块校验主体（承诺比对 vs ValidateBody）+ 单侧 DA 拒绝路径（B-5b） |
| 3 状态执行 | 等价 + 1 已知分叉 | D-1 交易级 + D-4 快照契约（契约已固化，不进块级字节） |
| 4 块级收尾 | 等价 + 1 已知分叉 | B 台账全关闭（B-1/B-8 已修一致）+ B-3 回执扩展字段 2 delta |
| 5 落库 | 结构性差异 | 索引隔离（SYS_HASH_2_TX 不写；对 getTransactionReceipt 可查性有影响） |
| 6 输出 | 等价 | block hash 承诺比对；output root 不在 EL 范围 |
```

- [ ] **Step 3: 自验 + Commit**

```bash
grep -c "^## §8" docs/opstack-opgeth-e2e-comparison.md  # 应 1
grep -n "2 结构性" docs/opstack-opgeth-e2e-comparison.md  # 阶段1/2 两处
git add docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w7): §8.1-8.2 总体结论 + 7 阶段判定表"
```

Expected: §8 头 + 判定表落位；阶段 1/2 含「2 结构性」；commit 成功。

---

### Task 2: §8.3 已知差异明细落定

**Files:**
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（追加 §8.3）

**Interfaces:**
- Consumes: DIVERGENCES D 项/B 项/结构性差异节；spec §4 §8.3
- Produces: §8.3（D/B 终态 + 结构性差异接受决策）

- [ ] **Step 1: 写 §8.3**

在 §8.2 后追加：

```markdown
### 已知差异明细落定

- **D 项终态**：D-1/D-3 等价（已确认）；D-2 Karst 🔴（阻塞，论证见上线闸）；D-4 已知分叉（契约已固化）
- **B 项终态**：B-1/B-8 已修一致；B-2/B-4/B-5a 事实达成（B-2/B-4 正式迁移在此落定）；B-3 已知分叉（2 delta，注记收紧归 W7）；B-5b/B-5c/B-6/B-7 已确认
- **结构性差异「接受」决策**：
  - 块校验位置：接受（承诺比对是 FISCO 架构选择，VALID/INVALID 语义互通无碍）
  - 双执行器并存：接受（v2 未装配，生产单路径）
  - **索引隔离：接受，但标注互通后果**——SYS_HASH_2_TX 刻意不写 → OP 块 `eth_getTransactionReceipt` 恒返回 null（当前分支未合 rawtx 回退/opReceiptMeta，fix 在 val-loop 未合并）；属功能性 RPC 缺口，入上线闸 gap 清单
  - PBFT 哑桩：接受（W3 门控已生效），但与「PBFT 共识层是否整体禁用」未决决策纠缠——纯 EL 无碍，自持共识上线需决策
```

- [ ] **Step 2: 自验 + Commit**

```bash
grep -n "getTransactionReceipt" docs/opstack-opgeth-e2e-comparison.md  # 索引隔离注记含
git add docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w7): §8.3 已知差异明细落定（含索引隔离互通后果）"
```

Expected: §8.3 落位，索引隔离注记含回执后果；commit 成功。

---

### Task 3: §8.4 Karst 上线闸

**Files:**
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（追加 §8.4）

**Interfaces:**
- Consumes: DIVERGENCES D-2；spec §4 §8.4（含 R2 修正）
- Produces: §8.4（gap 清单 + 排期 + Go/No-Go）

- [ ] **Step 1: 写 §8.4**

在 §8.3 后追加：

```markdown
### Karst 上线闸

1. **gap 清单**（影响 / 工作量 / 阻塞性）：

| gap | 影响 | 工作量 | 阻塞性 |
|---|---|---|---|
| D-2 Karst 适配 | 高——FISCO 无 karstTime 激活通道（configAt/OpForkTimestamps/NodeConfig/Initializer 全缺）+ karstConfig 调度路径死代码 + 前向兼容风险；op-geth v1.101702.2 的 Karst 是纯 config fork（IsKarst 零行为调用点） | 中高 | 🔴 阻塞 |
| OP 块回执不可查 | 高——SYS_HASH_2_TX 刻意不写 → eth_getTransactionReceipt 恒 null；fix 在 val-loop（rawtx 回退 + opReceiptMeta）未合并 | 中 | 🔴 生产阻塞 |
| PBFT 共识层未决 | 中——自持共识上线阻塞；纯 EL 视角可降级互通项 | 中 | 视上线形态 |
| B-2/B-4 正式迁移 | 低（W7 内完成） | 低 | 否 |
| B-3 注记收紧 | 低（W7 内完成） | 低 | 否 |
| deferred minors（cases/ gitignore、golden manifest 校验、首投 B 软断言） | 低 | 低 | 否 |

2. **修复排期**：
```
Karst 适配（专项）→ 重跑 W5 gate（回归）→ 重新对拍（Karst 专项金标准）→ 可上线评估
```
3. **Go/No-Go**：**当前 No-Go**——FISCO 无法激活/表征 Karst（无 karstTime 通道）+ OP 块回执不可查。附条件：① Karst 适配完成（引入 karst_time 激活通道 + 按 op-geth 真实 diff）② W5 gate 回归通过 ③ 重新对拍（仿 B-5c 的 Karst 链式对 + Karst 金标准 golden + op-geth 版本 pin）通过 ④ OP 块回执可查 → 重新评估可上线。
```

- [ ] **Step 2: 自验 + Commit**

```bash
grep -n "No-Go\|重新对拍\|回执不可查" docs/opstack-opgeth-e2e-comparison.md
git add docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w7): §8.4 Karst 上线闸（gap 清单 + 排期 + Go/No-Go）"
```

Expected: §8.4 三件套齐全；回执项 + 重新对拍 + No-Go 落位；commit 成功。

---

### Task 4: §8.5 待办移交 + §8 定稿

**Files:**
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（追加 §8.5 + 一致性终检）

**Interfaces:**
- Consumes: spec §4 §8.5；W6 §7「W6 外待办」
- Produces: §8.5 + 最终 §8 定稿

- [ ] **Step 1: 写 §8.5**

在 §8.4 后追加：

```markdown
### 待办移交（W7 之后）

- **Karst 适配专项任务**（引入 karst_time 激活通道，按 op-geth 真实 diff）
- **OP 块回执可查修复**（rawtx 回退 + opReceiptMeta，从 val-loop 移植）
- **PBFT 共识层决策**（是否整体禁用 + retry loop 抑制）
- deferred minors 清理（cases/ gitignore、golden manifest 校验、首投 B 软断言）
- 生产互通待办（V4 端点桩 / V4 能力广播 / generator 重生成——见 §7「W6 外待办」）
- **W8 / W0**（记忆遗留 ctest/落盘/四项决策 + DU 冲突清理）

> 注：B-2/B-4 正式迁移 + B-3 注记收紧已在 §8.3 W7 内完成，不入此移交。
```

- [ ] **Step 2: §8 一致性终检**

逐节检查：§8 含 5 小节；7 阶段判定每行有依据（引用矩阵）；上线闸三件套齐全；索引隔离注记含回执后果；B-2/B-4/B-3 标注 W7 内完成；§8.5 含 W8/W0；标题格式对齐 §7。缺则补。

```bash
grep -c "^### \|^## §8" docs/opstack-opgeth-e2e-comparison.md  # §8 5 小节
grep -n "W8\|W0\|回执可查修复\|PBFT 共识层决策" docs/opstack-opgeth-e2e-comparison.md
git add docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w7): §8.5 待办移交 + §8 定稿"
```

Expected: §8 五小节齐全、W8/W0/回执修复落位；commit 成功。

---

### Task 5: 多 agent 证据级审查 + 修复

**Files:**
- Modify: `docs/opstack-opgeth-e2e-comparison.md`（§8 修复）
- Consumes: 最终 §8；DIVERGENCES / W5 测试 / W6 §7

**Interfaces:**
- Consumes: Task 1-4 的 §8 定稿
- Produces: 审查通过后的 §8 终稿

- [ ] **Step 1: 派 3 个证据级审查 agent**

按 spec §6（对齐 comparison doc §0.0 先例），派 3 个独立审查 agent 审计 §8，每个聚焦一维（均要求 file:line + 源码摘录）：
1. **阶段判定 vs DIVERGENCES**：§8.2 每行判定/状态对照矩阵行（含阶段 1/2「2 结构性」、阶段 3/4「已知分叉」）
2. **上线闸 vs 实际代码**：D-2 论证（无激活通道/死代码）、回执不可查（EthEndpoint.cpp:757-787）、Go 条件完备性
3. **§8.5 移交 vs W6/W5**：生产互通待办与 §7「W6 外待办」一致、W8/W0 在列、deferred minors 具体

审查 prompt 模板：给 agent 以下输入——§8 文本位置、对应三输入文件、Global Constraints（阶段聚合修正/桥接/索引隔离注记/D-2 论证/Go 条件/§8.5 清单），要求输出「逐项 ✅/❌/⚠️ + file:line 证据」。

- [ ] **Step 2: 汇总 findings + 修复**

汇总 3 agent findings，按 Critical/Important/Minor 分级。Critical/Important 修复进 §8（改判定/补证据/修论证）；Minor 记入 SDD ledger deferred。修完重跑自验。

- [ ] **Step 3: Commit**

```bash
git add docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs(w7): §8 多 agent 证据级审查修复"
```

Expected: §8 经 3 agent 审查后修复提交；Critical/Important 清零（若有则再一轮）。

---

## 执行顺序与验收

```
Task 1 (§8.1+8.2) → Task 2 (§8.3) → Task 3 (§8.4) → Task 4 (§8.5+定稿) → Task 5 (多 agent 审查+修复)
```

- 每任务独立可审：T1 靠 grep §8 头/判定表；T2 靠索引隔离注记；T3 靠三件套 + No-Go；T4 靠五小节 + W8/W0；T5 靠审查 findings 闭环
- 验收（spec §6）：§8 五小节 + 7 阶段判定有依据 + 上线闸三件套（含回执项）+ D-2 阻塞（激活通道论证）+ 多 agent 证据级审查通过（Task 5）
- 审查：SDD task-reviewer 逐任务 + 最终 whole-branch review（opus）审计 §8 判定证据链
