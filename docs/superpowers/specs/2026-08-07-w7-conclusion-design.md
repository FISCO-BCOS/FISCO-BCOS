# W7：结论定稿 + Karst 上线闸设计

> 目标：把三层对拍（W4 L0 静态矩阵 + W5 L1 动态 gate + W6 L2 e2e）汇总成最终裁决——7 阶段三态判定 + 已知差异明细落定 + **Karst 上线闸**（gap 清单 → 修复排期 → Go/No-Go），产出 comparison doc §8。
> 状态：**Approved**。日期：2026-08-07。
> 关联：W4 `DIVERGENCES.md`（矩阵 + B 台账 + D 项）、W5（M-B 台账，B-5b/5c/B-7/D-4 关闭）、W6 §7（L2 报告）。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

对比链路三层已全部交付：
- **W4 L0**：7 阶段差异矩阵 + B 台账 + 结构性差异 + D 项（`DIVERGENCES.md`，13 节）
- **W5 L1**：M-B 台账，B-5b/5c/B-7/D-4 动态收口，B 台账「待W5」全关闭（⚠️ R3：计数口径 = W6 §7「OP 145/145」+ W5 新增 4 用例 ≈ 149，W7 引用时用可追溯口径）
- **W6 L2**：34 用例 e2e + 3 真实 parity bug 修复（§7 报告）

但结论仍是**分项的**——没有人把它们汇总成「FISCO OP 执行器到底与 op-geth 等不等价、能不能上线 Karst」的最终裁决。W7 做这件事：**结论定稿 + 上线闸**。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | **产出 = comparison doc 追加 §8**（与 §7 W6 报告并列，读者一处看全） | 用户选定 |
| **D2** | **上线闸 = 完整决策框架 + Go/No-Go**（gap 清单 → 修复排期 → Go/No-Go 附条件） | 用户选定 |
| **D3** | **D-2 Karst 占位 = 列 gap + 排期**（阻塞项列出，实际适配另立专项任务） | 用户选定 |
| **D4** | W7 是**裁决不是执行**——不写适配代码，只定稿判定 + 排期 | 设计 |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `docs/opstack-opgeth-e2e-comparison.md` | **修改**：追加 §8 结论定稿 + Karst 上线闸（工作区已跟踪的 worktree 副本） |

## 4. §8 结构

### §8.1 总体结论
一句话总结论：FISCO OP 执行器**核心执行路径（阶段 0/3/4）与 op-geth v1.101702.2 逐位一致**，差异集中在**结构层设计（索引隔离/块校验位置/双执行器）**与 **Karst 未适配（D-2 🔴）**。

> ⚠️ W7 审查（R3）：「逐位一致」**限定于块级共识字节**（stateRoot/receiptsRoot/withdrawalsRoot/encodeOpHeader）——D-4（快照时序契约）与 B-3（RPC 扩展字段）是矩阵级「已知分叉」，**不进块级字节**，故阶段 3/4 在共识口径下「等价」成立。

### §8.2 7 阶段三态判定表

| 阶段 | 聚合判定 | 依据（引用 DIVERGENCES 矩阵行） |
|---|---|---|
| 0 数据形态 | 等价 + 1 结构性 | 三次类型翻译（结构性，已确认） |
| 1 RPC 入口 | **等价 + 2 结构性**（⚠️ R1：注册 V4桩/V5缺 + 校验位置两行） | 注册（V4 桩/V5 缺失）、差异点：校验位置（执行后 vs catalyst 层） |
| 2 块校验 | **等价 + 2 结构性**（⚠️ R1：承诺比对主体 + DA 拒绝路径 B-5b） | 块校验主体（承诺比对 vs VerifyHeader/ValidateBody）、单侧 DA 拒绝路径（B-5b） |
| 3 状态执行 | **等价 + 1 已知分叉**（⚠️ R1/R3：D-4 快照契约） | D-1 交易级 + D-4 契约已固化（M-D4）——分叉不进块级字节 |
| 4 块级收尾 | **等价 + 1 已知分叉**（⚠️ R1/R3：B-3 回执扩展字段） | B 台账全关闭（B-1/B-8 已修一致）+ B-3 2 delta——分叉不进块级字节 |
| 5 落库 | 结构性差异 | 索引隔离（设计取舍，SYS_HASH_2_TX 不写；**对 getTransactionReceipt 可查性有影响**） |
| 6 输出 | 等价 | block hash 承诺比对；output root 不在 EL 范围 |

### §8.3 已知差异明细落定
- **D 项终态**：D-1/D-3 等价、D-2 Karst 🔴（阻塞，论证见 §8.4）、D-4 已知分叉（契约已固化）
- **B 项终态**：B-1/B-8 已修一致、B-2/B-4/B-5a 事实达成（**B-2/B-4 正式迁移均留此节**，⚠️ R3：W6 §7.2 把 B-4 也留给 W7）、B-3 已知分叉（2 delta）、B-5b/B-5c/B-6/B-7 已确认、B-3 注记收紧
- **结构性差异「接受/处理」决策**：
  - 块校验位置（接受——承诺比对是 FISCO 架构选择，VALID/INVALID 语义互通无碍）
  - 双执行器并存（接受——v2 未装配，生产单路径）
  - **索引隔离（接受，但 ⚠️ R2/R3 补互通后果）**：SYS_HASH_2_TX 刻意不写 → **OP 块 `eth_getTransactionReceipt` 恒返回 null**（当前分支未合 rawtx 回退/opReceiptMeta，fix 在 val-loop 未合并）——这不是纯架构取舍，是**功能性 RPC 缺口**，挂入 §8.4 gap 清单
  - **PBFT 哑桩（接受，但 ⚠️ R2 表面未决）**：W3 门控已生效，但与「PBFT 共识层是否整体禁用」未决决策纠缠——纯 EL 视角无碍，自持共识上线视角需决策

### §8.4 Karst 上线闸（完整决策框架）
> ⚠️ W7 审查（R2）：闸的作用域 = **EL 执行器等价 + Karst 就绪**——生产互通待办（V4 端点/PBFT retry）归 §8.5，但「回执可查性」与「PBFT 共识未决」是生产上线问号，显式挂入。

1. **gap 清单**（每项：影响 / 工作量 / 阻塞性）：

| gap | 影响 | 工作量 | 阻塞性 |
|---|---|---|---|
| D-2 Karst 适配（⚠️ R2 论证改写：**无 karstTime 激活通道 + karstConfig 死代码 + 前向兼容风险**） | 高——FISCO 无法表征/激活 Karst（configAt/OpForkTimestamps/NodeConfig/Initializer 全缺 karst_time；karstConfig 调度路径死代码）；op-geth v1.101702.2 的 Karst 是纯 config fork（IsKarst 零行为调用点，与 Jovian 全等），但生产 Karst 链跑更新 op-geth，Karst 语义迟早落地，FISCO 无机制感知 | 中高 | 🔴 阻塞 |
| **OP 块回执不可查**（⚠️ R2 漏项，最高优先）：SYS_HASH_2_TX 刻意不写 → `eth_getTransactionReceipt` 恒 null；fix 在 val-loop 已落地（rawtx 回退 + opReceiptMeta）未合并 | 高——功能性 RPC 缺口，影响块浏览器/索引器/op-node 下游 | 中 | 🔴 生产阻塞 |
| PBFT 共识层未决（⚠️ R2）：「是否整体禁用」决策 + retry loop | 中——自持共识上线阻塞；纯 EL 视角可降级互通项 | 中 | 视上线形态 |
| B-2/B-4 正式迁移（⚠️ R4：W7 内完成，非移交——§8.3 落定） | 低——事实达成，正式定稿 | 低 | 否 |
| B-3 注记收紧（⚠️ R4：W7 内完成——§8.3 改「动态 manifest 归 W7」） | 低 | 低 | 否 |
| deferred minors（cases/ gitignore ← W5 D#2、golden manifest 校验 ← W5 regen.sh、**首投 B 软断言** ← W5 T3，⚠️ R2 补全） | 低 | 低 | 否 |

2. **修复排期**（依赖顺序）：
```
Karst 适配（专项）→ 重跑 W5 gate（回归）→ 重新对拍（Karst 专项金标准）→ 可上线评估
```
3. **Go/No-Go**：**当前 No-Go**——FISCO 无法激活/表征 Karst（无 karstTime 通道）+ OP 块回执不可查。附条件（⚠️ R2 补第 3 步）：① Karst 适配完成（引入 karst_time 激活通道 + 按 op-geth 真实 diff 适配）② W5 gate 回归通过 ③ **重新对拍（仿 B-5c 的 Karst 链式对 + Karst 金标准 golden + op-geth 版本 pin）通过** ④ OP 块回执可查 → 重新评估可上线。

### §8.5 待办移交（W7 之后）
- **Karst 适配专项任务**（引入 karst_time 激活通道，按 op-geth 真实 diff）
- **OP 块回执可查修复**（rawtx 回退 + opReceiptMeta，从 val-loop 移植）
- **PBFT 共识层决策**（是否整体禁用 + retry loop 抑制）
- deferred minors 清理（cases/ gitignore、golden manifest 校验、首投 B 软断言）
- 生产互通待办（V4 端点桩 / V4 能力广播 / generator 重生成——comparison doc §7「W6 外待办」已记，⚠️ R4：指针为 §7 非 §7.3）
- **W8 / W0**（⚠️ R4：独立收尾项补入移交）

> ⚠️ R4：B-2/B-4 正式迁移 + B-3 注记收紧为 **W7 内完成**（§8.3），不入 §8.5 移交。

## 5. 流程

1. 汇总三输入（DIVERGENCES + W5 M-B + W6 §7）
2. 起草 §8（§8.1-8.5）
3. 多 agent 证据级审查：每个三态判定/差异明细/上线闸条目对照矩阵/W5/W6 复核
4. 定稿 + commit

## 6. 验收标准

- §8 含 5 节（总体结论/7 阶段判定/差异落定/上线闸/待办移交）；§8 顶级标题对齐 §7 格式（`## §8 结论定稿 + Karst 上线闸（W7，2026-08-07）`）
- 7 阶段判定每个有依据（引用 DIVERGENCES 矩阵行 + 三态精确，含已知分叉行）
- 上线闸含 gap 清单（影响/工作量/阻塞性，**含回执不可查项**）+ 修复排期 + Go/No-Go（附条件含重新对拍）
- D-2 Karst 列为阻塞 gap（论证 = 无激活通道 + 死代码 + 前向兼容）+ 适配另立任务声明
- **多 agent 证据级审查通过**（⚠️ R4：触发机制——plan 定 agent 数/证据阈值/裁决人，对齐 comparison doc §0.0 先例；每个三态判定/上线闸条目对照 DIVERGENCES/W5/W6 复核）

## 7. 不在 W7 范围

- **Karst 实际适配**（D3，另立专项任务）
- **生产互通待办实现**（V4 端点/PBFT retry——W6 §7.3 已记，另立）
- **W8/W0**（记忆遗留/DU 清理——独立收尾）
- 不写任何适配代码（D4）
