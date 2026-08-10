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
| `bcos-evm/test/opstack/t8n/generator/cases.go` | **修改**：加 jovian 链式双块 case + order-observable case（复用 4788 预部署 + 新增用户 reader 合约） |
| `bcos-evm/test/opstack/t8n/generator/main.go` | **修改**：`processChainPair` 参数化 jovian（`:742` 硬编码 isthmus → fork 参数 + jovian attributes 布局 + `daScalar>0` + MinBaseFee 旋钮） |
| `bcos-evm/test/opstack/t8n/generator/regen.sh` | **修改**（⚠️ R1：现状只重放 33 vectors/ 不产 golden）：加 `--golden-output` + `--chain-output-dir` 调用、`[ $n -eq 33 ]` 计数 → 34、cases↔manifest diff 更新 |
| `bcos-evm/test/opstack/t8n/golden/engine/manifest.txt` | **修改**：B-7 新 case 加条目 |
| `bcos-evm/test/opstack/t8n/golden/engine/SHA256SUMS` | **修改**：regen 后刷新（含新 golden） |
| `bcos-evm/test/opstack/t8n/golden/engine/`（新 golden） | **新增**：jovian 链式对（`chained/jovianChainA/B.golden.json`）+ order-observable 向量 |
| `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp` | **修改**：追加 B-5c jovian 链式用例 + B-7 order-observable 用例；`runChainedPair` 放宽 jovian（`:243/:244`） |
| `bcos-evm/test/opstack/OpL1EdgeGateTest.cpp` | **新增**：B-5b 拒绝用例 + D-4 快照契约用例（⚠️ 无法复用 `OpE2eFixture`（匿名 namespace）——B-5b 需自建/抽共享 fixture，D-4 用 TestState 模式） |
| `bcos-evm/test/CMakeLists.txt` | **修改**：`OpL1EdgeGateTest.cpp` 加进 `bcos-evm-opstack-tests`（只需加源，W6 段已含 jsoncpp/EngineHelper 等） |
| `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md` | **修改**：B-5b/5c/B-7/D-4 状态升级 + 追加 M-B 块级台账 |

## 4. 四个项的测试设计

### 4.1 B-5b — ≤GasLimit 拒绝路径

- **构造**：取既有 jovian 向量（如 `jovian_da_mix`，DA=593600，gasLimit=10M），把 `blobGasUsed` 改为 `> gasLimit`（如 gasLimit+1）
- **断言**：`newPayload(4)` 返回 **INVALID**（`EngineServiceImpl.cpp:442-444` 拒绝路径触发），`validationError` 含 **`"DA footprint"`**（⚠️ R2：实际串 `"DA footprint (blobGasUsed) exceeds the block gas limit"`——中间夹 `(blobGasUsed)`，勿用 `"DA footprint exceeds"` 子串）
- **触发确认**：改 blobGasUsed 能精确触发（`:324` present 通过、`:328` jovian 分支关闭、`:350/:363` gasLimit 界内、`:428` uint64 内、`:442` 拒绝）；excessBlobGas 校验（`:320`）不受影响（只改 blobGasUsed）
- **免新 golden**：payload 改自既有向量，期望是拒绝（非金标准比对）
- **落点**：`OpL1EdgeGateTest.cpp`

### 4.2 B-5c — 跨块 Jovian baseFee max()

> ⚠️ W5 审查（R1/R2）修正：① jovian 链式对 = **参数化 `processChainPair`**（`main.go:742` 硬编码 `buildChainConfig("isthmus")`）——需 MinBaseFee 旋钮 + jovian attributes 布局（`attributesTx(...,true)`）+ `daScalar>0` + 大 calldata tx，**非仅改 fork 时间戳**；② block1 必须 **DA footprint > gasUsed**（非仅非零）才使 max 分支可观测（若 DA<gasUsed 则 `parentGasMetered=gasUsed` 无差别）；③ 断言措辞：真正的验证是 **step 3a-2 baseFee 一致性校验通过**（`EngineServiceImpl.h:945-956` 用存储中的 parent header 调 `calcOpBaseFee` 与 `payload.baseFeePerGas` 比较，失配 → block2 INVALID），非 `assertSevenFields` 的 baseFee 字段比对（后者从 golden payload 重构，平凡恒等）。

- **generator case**：参数化 `processChainPair` 产 jovian 双块——block1 jovian（**DA > gasUsed**，如 da_mix 的 593600 vs 146140，且 DA < gasLimit），block2 jovian（baseFee = `max(parentGasUsed, parentDAFootprint)`，`EngineServiceImpl.cpp:233-240`）
- **golden**：regen 从 op-geth 产（block2 的 baseFee 是 op-geth `calcBaseFeeInner` max 分支金标准）
- **断言**：顺序投递 block1 → VALID、block2 → **VALID**（step 3a-2 baseFee 一致性校验通过即验证 max 分支）+ 七项全等
- **runChainedPair 放宽**：`BOOST_REQUIRE(!sampleA.jovian && !sampleB.jovian)`（`OpNewPayloadRpcE2eTest.cpp:243`）→ `BOOST_REQUIRE(sampleA.jovian == sampleB.jovian)` + `forkTimestampsFor(sampleA.jovian)`（:244）
- **落点**：W6 harness（复用 `runChainedPair` 模式）

### 4.3 B-7 — 系统调用顺序可观测性

> ⚠️ W5 审查（R2）修正：读路径判别力更强——EIP-4788 beaconRootsCode 读路径（PC 36-72）**先校验 `w == storage[w%8191]`，失配即 REVERT**。若用户 tx 先于系统调用，读到 stale/0 → 用户合约 REVERT → stateRoot 失配 → 测试红。用户 reader 合约须**传当前块 timestamp 作 calldata**（读路径校验 `w==storage[w%8191]`），写入槽须声明 `ExtraStorage`。复用 `system_contracts_real` 的 4788 预部署（`cases.go:472-508`，beaconRootsCode + stale ring 槽），**新增**用户 reader 合约。

- **generator case**：复用 `system_contracts_real` 的 EIP-4788 预部署（beaconRootsCode @ 0xfff...ffe + stale ring 槽）；**新增**用户 reader 合约（bytecode 计算 `t%8191`、CALL 0xfff...ffe、SSTORE 自有槽）+ 用户 tx 调用（传当前 timestamp 作 calldata）+ `ExtraStorage` 声明
- **golden**：regen 从 op-geth 产（**⚠️ 单 fork isthmus**——W5 审查裁决：bothForks 会使向量计数矛盾（34 vs 35），且顺序可观测性 fork 无关；case id = `isthmus_system_call_order_observable`）——顺序错则读路径 REVERT → stateRoot 失配 → 测试红
- **断言**：VALID + 七项全等（顺序正确则 stateRoot 匹配 golden）
- **落点**：W6 harness

### 4.4 D-4 — 快照契约单测

> ⚠️ W5 审查（R3）修正：**对象由 `processOpBlock` 改为 `opValidate`/`opTransition` 函数对**。「validate 后 transition 前改槽」对 processOpBlock 不可行（normal-tx 分支内两者背靠背调用 `OpBlockExecute.cpp:185/:194` 无插入缝隙；且 opTransition 不重读存储、只消费 props，改槽天然惰性）。同时修正：fee 在**首笔 normal tx** 处懒加载（`:152 if(!feeLoaded)` → `:157 loadOpFeeParams`，非首笔 attributes）；**op-geth 也是每块缓存**（非"即时读"），D-4 是 FISCO 内部回归护栏，op-geth 侧等价由 W6 golden 覆盖。

- **对象**：`opValidate`/`opTransition` 直接函数对（仿 `OpTransitionTest` 模式，测试体即缝隙）
- **方法**：
  1. `opValidate` 注入 fee F（取差异明显的 `l1_base_fee`/scalar）→ 断言 `props.fee == F`（快照冻结进 `OpTxProperties.fee`）
  2. 改 `ts[OP_L1_BLOCK].storage[slot1/slot3]` 为显著不同的 F'
  3. `opTransition`（消费 props，`OpTransition.cpp:211 props.fee`）
  4. 断言回执 opStackMeta 的 `l1_fee == 按 F 计算值`（`props.l1_cost`），而非按 F'
- **判别力**：回归「opTransition 重读存储」即变红
- **mock**：`test::TestState` + `seedOpPredeploys`（OpTransitionTest 全文件模式），无需 scheduler/MLS/harness
- **落点**：`OpL1EdgeGateTest.cpp`（include `OpTransition.h`，复用 TestState 模式，非 mock scheduler）

## 5. 流程

```
1. 扩展 generator：cases.go 加 B-5c jovian 链式 + B-7 order-observable case；main.go 参数化 processChainPair（jovian）
2. 扩展 regen.sh（加 --golden-output + --chain-output-dir + 计数 34 + manifest）→ 跑 regen → op-geth 产新 golden + 刷新 SHA256SUMS/manifest
3. 新 golden 进 W6 harness（B-5c/B-7 用例 + runChainedPair 放宽 jovian）
4. 新建 OpL1EdgeGateTest.cpp（B-5b 拒绝 + D-4 契约）
5. DIVERGENCES.md：B-5b/5c/B-7/D-4 状态升级 + M-B 块级台账（schema 见 §6）
```

## 6. 验收标准

- B-5b：`blobGasUsed > gasLimit` 向量返回 INVALID + validationError 含 `"DA footprint"`（拒绝路径触发）
- B-5c：jovian 链式对 block1+block2 全 VALID（**step 3a-2 baseFee 一致性校验通过**即验证跨块 max 分支）+ 七项全等；block1 DA > gasUsed
- B-7：order-observable 向量 VALID + 七项全等（顺序正确，stateRoot 匹配）
- D-4：快照契约单测绿（opValidate 注入 F → 改槽 F' → opTransition 用 props 按 F 计价）
- DIVERGENCES.md：4 项状态从「待W5」升级 + **M-B 块级台账 4 条**（⚠️ R4：schema 仿 B 台账——列 = `M-B项 | 验证手段 | 断言 | 用例/golden | 结果状态`）
- 全量回归：既有 OP 145/145 + 新增用例全绿

## 7. 不在 W5 范围

- **output root**（op-node 侧，出 EL 对比范围）
- **op-reth**（第二 oracle，另立任务）
- **B-2/B-3**——B-2 事实达成留 W7 定稿；**B-3 的「动态 manifest 待 W5/RPC 层对拍」注记改派 W7/RPC 层对拍**（W5 明确不移入）
- **阶段3 #2 fee 惰性加载行**（DIVERGENCES.md:75，等价/待W5，D-4 相关）——**关闭路径**：D-4 断言「opTransition 不重读存储（用 props 快照）」+ 既有 33 向量 stateRoot 逐位匹配，共同关闭该行（D-4 测试即其固化）
- 生成器架构重构（只加 case + 参数化 chain）

## 8. 风险与缓解

| 风险 | 缓解 |
|---|---|
| generator 跑 op-geth 需 Go module 设置 | op-geth go.mod + Go 1.24 toolchain 已缓存，`go build ./cmd/opt8n-ref` 可跑（R1 核实） |
| B-7 预部署合约构造复杂 | 复用 `system_contracts_real` 的 4788 预部署 + 新增用户 reader 合约（R2 核实） |
| B-5c generator 链式支持 | `processChainPair` 已有链式机制，需参数化 jovian（MinBaseFee/jovian attributes/daScalar/大 calldata，R1/R2 核实） |
| D-4 mock 存储复杂度 | `test::TestState` + `seedOpPredeploys`（OpTransitionTest 模式），无需 harness/MLS（R3 核实） |
