# W5：L1 差分 gate 块级断言扩充设计

> 目标：把 W4 差异矩阵的 4 项「待W5」（B-5b/B-5c/B-7/D-4）用动态验证收口——新 golden（jovian 链式 + order-observable）+ 拒绝用例 + 快照契约单测，产出 DIVERGENCES.md 块级台账（M-B 系列）。
> 状态：**Approved**。日期：2026-08-07。
> 关联：W4 `DIVERGENCES.md` B 台账；W6 harness `OpNewPayloadRpcE2eTest`（34 用例）。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

W4 差异矩阵 B 台账留下 4 项「待W5」——静态判不了、需动态验证：

| 项 | 缺口 | 验证手段 |
|---|---|---|
| **B-5b** | ≤GasLimit 拒绝路径未触发（W6 全 VALID 正向向量） | 拒绝向量（`blobGasUsed > gasLimit` → INVALID） |
| **B-5c** | 跨块 Jovian baseFee `max(parentGasUsed,parentDAFootprint)` 无链式对（harness 强制 isthmus） | jovian 链式对（新 golden） |
| **B-7** | 系统调用顺序仅结构确认，顺序不可观测 | order-observable 向量（用户 tx 读 beaconRoot） |
| **D-4** | 快照契约（validate/transition 共享快照）无断言固化 | processOpBlock 单测 |

W5 把这些收口，把「待W5」升级为已验证，并产出 M-B 块级台账——对拍结论固化成持续回归护栏。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | **四项全做**（B-5b/B-5c/B-7/D-4） | 用户选定；W4 B 台账待W5 全收口 |
| **D2** | **新 golden = 扩展 generator + regen**——t8n/generator `cases.go` 加 jovian 链式 + order-observable case，跑 regen 从 op-geth v1.101702.2 产金标准 | 用户选定；Go 1.23.4 + op-geth go.mod 可用，产出是 op-geth 真输出证据最强 |
| **D3** | **混合落点**——B-5c/B-7 新向量用例进 W6 harness（复用 fixture/seed/assert）；B-5b 拒绝 + D-4 契约（断言路径不同）进新文件 `OpL1EdgeGateTest.cpp` | 用户选定 |
| **D4** | B-5b 免新 golden（从既有 jovian 向量构造，改 blobGasUsed） | 设计 |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `bcos-evm/test/opstack/t8n/generator/cases.go` | **修改**：加 jovian 链式双块 case（block1+block2 jovian，block2 baseFee=max）+ order-observable case（预部署合约含 EIP-4788 beaconRoot 读取，用户 tx 读） |
| `bcos-evm/test/opstack/t8n/generator/regen.sh` | **运行**：产新 golden（新增 `t8n/golden/engine/` 文件 + SHA256SUMS 更新） |
| `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp` | **修改**：追加 B-5c jovian 链式用例 + B-7 order-observable 用例（VALID + 七项 + 跨块 baseFee 断言） |
| `bcos-evm/test/opstack/OpL1EdgeGateTest.cpp` | **新增**：B-5b 拒绝用例 + D-4 快照契约用例 |
| `bcos-evm/test/CMakeLists.txt` | **修改**：`OpL1EdgeGateTest.cpp` 加进 `bcos-evm-opstack-tests` |
| `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md` | **修改**：B-5b/5c/B-7/D-4 状态升级 + 追加 M-B 块级台账 |

## 4. 四个项的测试设计

### 4.1 B-5b — ≤GasLimit 拒绝路径

- **构造**：取既有 jovian 向量（如 `jovian_da_mix`，DA=593600，gasLimit=10M），把 `blobGasUsed` 改为 `> gasLimit`（如 gasLimit+1）
- **断言**：`newPayload(4)` 返回 **INVALID**（`EngineServiceImpl.cpp:442-444` 拒绝路径触发），`validationError` 含 "DA footprint exceeds"
- **免新 golden**：payload 改自既有向量，期望是拒绝（非金标准比对）
- **落点**：`OpL1EdgeGateTest.cpp`

### 4.2 B-5c — 跨块 Jovian baseFee max()

- **generator case**：jovian 链式双块——block1 jovian（DA footprint 非零），block2 jovian（baseFee = `max(parentGasUsed, parentDAFootprint)`，`EngineServiceImpl.cpp:233-239`）
- **golden**：regen 从 op-geth 产（block2 的 baseFee 是 op-geth 金标准）
- **断言**：顺序投递 block1 → VALID、block2 → VALID + 七项全等 + **block2 baseFee == golden**（跨块 max 分支验证）
- **落点**：W6 harness（复用 `runChainedPair` 模式，但允许 jovian + 非 isthmus 强制）

### 4.3 B-7 — 系统调用顺序可观测性

- **generator case**：预部署合约（仿 `system_contracts_real`）带 EIP-4788 beaconRoot ring buffer 读取逻辑；用户 tx 调用该合约读 beaconRoot
- **golden**：regen 从 op-geth 产——若系统调用顺序错（用户 tx 先于 beaconRoot 写入），读到的值不同 → stateRoot 失配 → 测试红
- **断言**：VALID + 七项全等（顺序正确则 stateRoot 匹配 golden）
- **落点**：W6 harness

### 4.4 D-4 — 快照契约单测

- **对象**：`processOpBlock`（`OpBlockExecute.cpp:99`）的 fee 快照机制（`feeLoaded` :129/:181，`loadOpFeeParams` :157）
- **方法**：mock 存储——在 validate 之后、transition 之前改变 fee 槽（L1 attributes 的 scalar），断言 transition 用的仍是**快照**（第一次读的值）而非改变后的值
- **断言**：若 transition 用了改变后的值 → 契约违反 → 测试红
- **落点**：`OpL1EdgeGateTest.cpp`（用 mock scheduler/storage，或复用 harness fixture）

## 5. 流程

```
1. 扩展 generator cases.go（B-5c jovian 链式 + B-7 order-observable）
2. 跑 regen.sh → op-geth 产新 golden + 更新 SHA256SUMS
3. 新 golden 进 W6 harness（B-5c/B-7 用例）
4. 新建 OpL1EdgeGateTest.cpp（B-5b 拒绝 + D-4 契约）
5. DIVERGENCES.md：B-5b/5c/B-7/D-4 状态升级 + M-B 块级台账
```

## 6. 验收标准

- B-5b：`blobGasUsed > gasLimit` 向量返回 INVALID（拒绝路径触发）
- B-5c：jovian 链式对 block1+block2 全 VALID + 七项全等 + block2 baseFee == golden（跨块 max 验证）
- B-7：order-observable 向量 VALID + 七项全等（顺序正确，stateRoot 匹配）
- D-4：快照契约单测绿（transition 用 validate 的同一快照）
- DIVERGENCES.md：4 项状态从「待W5」升级 + M-B 台账 4 条
- 全量回归：既有 OP 145/145 + 新增用例全绿

## 7. 不在 W5 范围

- **output root**（op-node 侧，出 EL 对比范围）
- **op-reth**（第二 oracle，另立任务）
- **B-2/B-3**（已确认/事实达成，W7 定稿）
- 生成器架构重构（只加 case）

## 8. 风险与缓解

| 风险 | 缓解 |
|---|---|
| generator 跑 op-geth 需 Go module 设置 | `regen.sh` 已有（generator 以 op-geth 为库） |
| B-7 预部署合约构造复杂 | 仿 `system_contracts_real` 已有模式（EIP-4788 bytecode） |
| B-5c generator 链式支持 | chainA/B 模式已有，改 jovian fork 时间戳 |
| D-4 mock 存储复杂度 | 复用 harness fixture（MLS + seed）或独立 mock scheduler |
