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
| **D5** | 7 阶段矩阵以 comparison doc §1 锚点对为基础，但**必须先按 §5 流程第 1 步的校正清单修正锚点**（W4 审查 R1/R2 证实 §1 约半数 FISCO 锚点偏移/指向不存在文件、op-geth 有 1 处真错锚） | 复用已核锚点 + 审查修正 |

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

**单侧行约定**（W4 审查 R4）：op-geth 单侧行（阶段2 extraData/base fee/DA footprint 等 FISCO 无对应列）→ 矩阵 FISCO 锚点列标「无对应 / 结构性差异」，判定为结构性差异或待W5。

**差异点段落归位规则**（W4 审查 R4）：comparison doc §1 每阶段「差异点」段落必须映射到 (a) 既有 B/D 项、(b) 矩阵行（给三态判定）、或 (c) 显式标注「已覆盖/不在范围」。已知孤儿须落定：
- 阶段3 #2 fee 参数惰性加载 vs op-geth L1Block 槽即时解析（`OpBlockExecute.cpp:150-157`）→ 新增矩阵行（判定：等价，D-4 相关）
- 阶段0 三次类型翻译 → 矩阵行（判定：结构性差异）
- 阶段1 校验位置（validateOpNewPayloadRequest vs checkOptimismPayload）→ 矩阵行（判定：结构性差异）

### 4.3 8 项 B 项台账

> ⚠️ W4 审查（4 agent）后修正：B-3/B-5/B-7 依据 W6 实际字节级覆盖细化。

| B 项 | 判定 | 状态 | 依据 |
|---|---|---|---|
| B-1 withdrawalsRoot | 等价 | **已修一致** | W6 分歧1（opStorageRoot 二次 RLP，`message_passer_write` isthmus+jovian 验证） |
| B-2 receiptsRoot | 等价 | **事实达成** | 承诺比对（EngineServiceImpl.h:1102-1105）+ 七项断言 33 向量；正式迁移留 W7 |
| B-3 回执字段 delta | 已知分叉（2 delta） | **已确认**（动态 manifest 待 W5/RPC 层对拍） | 2 delta 静态证据充分：`operator_fee`=FISCO 扩展（OpTransition.h:96-97 明标）；legacy `FeeScalar` 不在 `OpStackReceiptMeta`；W6 harness 不覆盖回执扩展字段（encodeReceiptForRoot 只共识编码） |
| B-4 gasUsed 回填 | 等价 | **事实达成** | 承诺比对（:1118-1121）+ 七项断言 33 向量 |
| B-5a DA header 写入 + == 校验 | 等价 | **事实达成** | `jovian_da_mix`（DA=593600=0x90ec0）字节级对拍通过（seal.blobGasUsed=Σ footprint，OpBlockSeal.cpp:187-197） |
| B-5b ≤GasLimit 拒绝路径 | 结构性差异-待验 | **待W5** | W6 全 VALID 正向向量，`blobGasUsed>gasLimit` 拒绝（EngineServiceImpl.cpp:442-444）未触发 |
| B-5c 下块 Jovian baseFee max() | 等价-待验 | **待W5** | 无 jovian 链式对（harness 强制链式双块 isthmus），baseFee max 分支（EngineServiceImpl.cpp:233-239）未演练 |
| B-6 da_footprint 读法 | 等价 | **已确认** | calldata[176:178] 提取（OpBlockExecute.cpp:174-179，静态证实） |
| B-7 系统调用顺序 | 等价（效果已证） | **待W5**（仅顺序可观测性） | 系统调用**效果**已由 stateRoot 逐位 golden 一致动态证实（`system_contracts_real` 33 向量）；残余缺口=**顺序可观测性**（需 order-observable 向量，如用户 tx 读 beaconRoot）；结构"先于首笔"由 OpBlockExecute.cpp:105-108 证实 |
| B-8 链式双块 | 等价 | **已修一致** | W6 链式对（state 延续+跨块 fee）+ 分歧2（txRoot 大块，HashBuilder.cpp:229-230） |

### 4.4 结构性差异（架构层）

- 块校验位置（执行后承诺比对 vs VerifyHeader/ValidateBody）
- 双执行器并存（OpstackExecutor.h vs executeOpBlock 单路径）
- 索引隔离（SYS_HASH_2_TX 故意不写）
- PBFT 双执行防护（throw 哑桩）

### 4.5 已确认项（D-1~D-4，直接纳入）

D-1 交易级逐位等价 / D-2 Karst 占位 🔴 / D-3 DA footprint 不进 tx 级 / D-4 费用快照契约 ⚠️

## 5. 流程

1. **锚点校正**（W4 审查 R1/R2 发现 §1 不能直接照抄）——按以下校正清单先产出一份「校正后锚点表」，作为矩阵底稿：

   **FISCO 侧**（本分支已重构/移动）：
   - 3 个文件已并入 `OpTransition.cpp`：`OpValidate.cpp:7` → `opValidate` @ `OpTransition.cpp:328`；`OpDepositTx.cpp:58` → `runDeposit` @ `:441`；`OpReceiptMap.h:120` → 回执映射 @ `:318-321`
   - `OpBlockSeal.cpp` 偏移：sealOpBlock @ `:138`、withdrawalsRoot 调用 @ `:170-174`、blobGasUsed @ `:187-197`（旧 :29/:60-64/:74-80 指向无关代码）
   - `OpTransition.cpp` 偏移 ~94：vault 路由 @ `:295-300`（旧 C-5 的 :186-191 也过期）
   - `OpBlockExecute.cpp` 偏移 24-45：processOpBlock @ `:99`、deposit 首笔 @ `:112-116`、blockGasLeft @ `:143/:198`、loadOpFeeParams @ `:157`、DA calldata @ `:164-180`
   - 微偏：`decodeOneRawTx` def @ `OpSchedulerImpl.h:855`（非 :857）；`calcOpBaseFee` def @ `EngineServiceImpl.cpp:191`（:233-239 是 Jovian max 逻辑）；`stateRootOf` def @ `StateRootCompute.h:76`；`LedgerMethods.h` 解引用 @ `:237`（非 :233-235）
   - 实质过时：缺口A 已修（`parseNewPayloadRequest` 现填 rawTransactions :71-76 / withdrawalsRoot :116-119）；`bcosTransactionToEvmone` 是通用路径转换器，OP 路径在 `OpTransition.cpp:456-457` 直接构造 evmone tx

   **op-geth 侧**（v1.101702.2）：
   - 🔴 `core/blockchain.go:2086` ProcessBlock → **`:2121`**（:2086 是 `bpr.Witness()`）
   - 🟡 路径补全：`consensus/misc/eip1559/eip1559_optimism.go`、`core/types/rollup_cost.go`（缺子目录/前缀）
   - 🟡 `receipt.go:199` → `state_processor.go:199`（MakeReceipt）

2. **逐阶段静态对拍**：每阶段派 subagent 按「校正后锚点表」核对双端源码（**含 §0.0 更正，§0.0 与正文冲突以 §0.0 为准**），产出 file:line 证据 + 三态判定
3. **差异点归位**：按 §4.2 归位规则把每阶段差异点段落映射进矩阵（阶段3 #2 孤儿等）
4. **汇总成 DIVERGENCES.md**（按 §4 结构）
5. **多 agent 审查**：证据级复核每个判定（每个三态判定需 file:line + 源码摘录支撑）

## 6. 验收标准

- DIVERGENCES.md 覆盖 7 阶段全部锚点（**用校正后锚点表，非 §1 原文**）+ 8 项 B 项（含 B-5a/b/c 拆分）+ 结构性差异 + D 项
- 每行判定有三态 + 状态图例 + file:line 证据
- **单侧行有「无对应/结构性差异」约定**；**差异点段落全部归位**（无孤儿）
- 无「未判」残留（静态判不了的明确标「待W5」——待W5 是状态图例，非判定占位）
- comparison doc §2 头部加指向 `DIVERGENCES.md` 的链接 + **B 项状态摘要表**（每 B 项最新状态一行），正文 B 项表保留
- 多 agent 审查通过（证据级）

## 7. 不在 W4 范围

- **op-reth**（D3，另立任务）
- **动态验证**（W5 的 t8n 断言扩充 / 逐字节金标准）
- **W7 结论定稿**（依赖 W4+W5+W6）
- 不重写 comparison doc §1/§2 正文（只加指向 + 摘要）
