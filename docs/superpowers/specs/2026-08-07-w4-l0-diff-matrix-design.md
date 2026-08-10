# W4：L0 静态对拍差异矩阵设计

> 目标：产出 FISCO opstack ↔ op-geth v1.101702.2 块执行流程的**系统性差异矩阵**（DIVERGENCES.md），把零散的对拍知识（§1 锚点、§2 B 项、W6 已证实分歧）整合成逐锚点三态判定清单。
> 状态：**Approved**。日期：2026-08-07。
> 关联：对比方案 `docs/opstack-opgeth-e2e-comparison.md` §3 L0 + §5 产出物#1；W6（L2 已交付，B-1/B-8 已修一致）。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

对比方案三层：L0 静态对拍（W4）→ L1 动态 gate（W5）→ L2 端到端（W6，已交付）。

W6 已动态证实并修复 3 个真实分歧（opStorageRoot 二次 RLP / ≥128 笔 txRoot 排序 / RTTI catch），B-1/B-8 转「已修一致」。但**差异知识仍是零散状态**——散在 comparison doc §1/§2/§7、记忆、修复注释里，没有一份逐锚点、可追溯、机器可读的完整矩阵。

W4 把这些整合成 `DIVERGENCES.md`：7 阶段 × 锚点的三态判定清单 + 8 项 B 项台账 + 结构性差异，作为 W5（动态 gate 的断言清单）与 W7（结论定稿）的权威底稿。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | **判定边界** = 静态能判则判，判不了标「待 W5 动态确认」——不强行下结论、不虚假完整 | 用户选定 |
| **D2** | **输出位置** = 新建 `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（§5 产出物#1 指定）；comparison doc §2 改为指向 + 摘要 | 用户选定 |
| **D3** | **op-reth 不入 W4**——保持 op-geth v1.101702.2 唯一锚定；如需对拍 op-reth 另立任务 | 用户选定 |
| **D4** | 判定三态 = `等价 / 已知分叉 / 结构性差异`；状态图例 = `已确认 / 已修一致 / 事实达成 / 待W5` | 设计 |
| **D5** | 7 阶段矩阵以 comparison doc §1 现有锚点对为基础（含 §0.0 审查更正的锚点），不重新勘探双端 | 复用已核锚点 |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md` | **新增**：7 阶段 × 锚点完整差异矩阵 + B 项台账 + 结构性差异 + 状态图例 |
| `docs/opstack-opgeth-e2e-comparison.md` | **修改**：§2 头部加「完整矩阵见 `DIVERGENCES.md`」指向 + 摘要，正文 B 项表保留 |

## 4. 矩阵结构

### 4.1 状态图例

| 状态 | 含义 |
|---|---|
| `已确认` | 静态/记忆审计证据充分（如 D-1 交易级、B-6 读法） |
| `已修一致` | W6 修复后与 op-geth golden 逐位一致（B-1/B-8） |
| `事实达成` | 非正式：七项断言在 33 向量逐位匹配，正式迁移留 W7（B-2/B-4） |
| `待W5` | 静态判不了，需 L1 动态/逐字节金标准确认（B-3/B-5/B-7） |

### 4.2 7 阶段矩阵（每阶段一张表）

```
| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
```

行 = comparison doc §1 各阶段锚点对（阶段0 数据形态 → 阶段1 RPC 入口 → 阶段2 块校验 → 阶段3 状态执行 → 阶段4 块级收尾 → 阶段5 落库 → 阶段6 输出）。

每行证据 = file:line + 一句源码结论；判定 = 三态之一；状态 = 图例之一。

### 4.3 8 项 B 项台账

| B 项 | 判定 | 状态 | 依据 |
|---|---|---|---|
| B-1 withdrawalsRoot | 等价 | **已修一致** | W6 分歧1（opStorageRoot 二次 RLP，`message_passer_write` isthmus+jovian 验证） |
| B-2 receiptsRoot | 等价 | **事实达成** | 七项断言 33 向量逐位匹配（§7 附注）；正式迁移留 W7 |
| B-3 回执字段 delta | 已知分叉（2 delta） | **待W5** | op-geth legacy `FeeScalar` FISCO 无；FISCO `operator_fee` op-geth 回执无 |
| B-4 gasUsed 回填 | 等价 | **事实达成** | 七项断言覆盖 |
| B-5 DA footprint 三处 | 等价（已实现） | **待W5** | 三处已实现（§0.0 C-2），golden 对拍 |
| B-6 da_footprint 读法 | 等价 | **已确认** | calldata[176:178] 提取（静态证实） |
| B-7 系统调用顺序 | 等价（结构） | **待W5** | 结构确认先于首笔，逐字节需 op-geth 输出 |
| B-8 链式双块 | 等价 | **已修一致** | W6 链式对（state 延续+跨块 fee）+ 分歧2（txRoot 大块） |

### 4.4 结构性差异（架构层）

- 块校验位置（执行后承诺比对 vs VerifyHeader/ValidateBody）
- 双执行器并存（OpstackExecutor.h vs executeOpBlock 单路径）
- 索引隔离（SYS_HASH_2_TX 故意不写）
- PBFT 双执行防护（throw 哑桩）

### 4.5 已确认项（D-1~D-4，直接纳入）

D-1 交易级逐位等价 / D-2 Karst 占位 🔴 / D-3 DA footprint 不进 tx 级 / D-4 费用快照契约 ⚠️

## 5. 流程

1. 逐阶段静态对拍：每阶段派 subagent 按 §1 锚点核对双端源码，产出 file:line 证据 + 判定
2. 汇总成 DIVERGENCES.md（按 §4 结构）
3. 多 agent 审查：证据级复核每个判定（每个三态判定需 file:line + 源码摘录支撑）

## 6. 验收标准

- DIVERGENCES.md 覆盖 7 阶段全部锚点 + 8 项 B 项 + 结构性差异 + D 项
- 每行判定有三态 + 状态图例 + file:line 证据
- 无「未判」残留（静态判不了的明确标「待W5」）
- comparison doc §2 有指向 + 摘要
- 多 agent 审查通过（证据级）

## 7. 不在 W4 范围

- **op-reth**（D3，另立任务）
- **动态验证**（W5 的 t8n 断言扩充 / 逐字节金标准）
- **W7 结论定稿**（依赖 W4+W5+W6）
- 不重写 comparison doc §1/§2 正文（只加指向 + 摘要）
