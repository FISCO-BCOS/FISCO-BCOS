# M0 台账 —— 12 个未定格清零（Plan A Task 1）

> 产物于 2026-09-12 由 Plan A 执行者取证落盘。oracle 检出与 pin：
> op-geth `blockchain-impl/op-geth` @ `e8800cffe`（= corpus golden pin）；
> optimism monorepo（op-node 5f90f749ca + op-revm 20.0.0）`blockchain-impl/optimism`；
> deposits 规格 `optimism/op-ai-obsidian/protocol/deposits.md`（原 specs/deposits.md 镜像）。
> 设计文档在只读 worktree（karst-on-5550），回写产物见
> `docs/plans/2026-09-12-plan-A-design-revision-pending.md`（待人工合入）。

### 格 1：F3/Granite 费用
- 本仓取值：`graniteConfig()` 沿用 `L1FeeModel::Fjord`（OpForkSchedule.cpp）
- 外部 oracle：op-geth `core/types/rollup_cost.go` 全文 fork 门只有
  `IsIsthmus`(:411)/`IsEcotone`(:426)/`IsFjord`(:436,:621)；`IsGranite` 0 命中于费用路径。
  op-revm `l1block.rs:286-306` `calculate_tx_l1_cost` 臂 = FJORD/ECOTONE/bedrock，无 GRANITE 臂。
- 结论：Granite **不改变费用臂** → `—`；Granite 的独立维度（operator fee 参数接受、deposit_exempt）
  由 M7 配置扫描（Task 5 的 granite 行）落回归。
- 影响：Task 5 granite 行 `feeModel=Fjord`；无新增 finding。

### 格 2：F3/Isthmus 费用
- 本仓取值：`isthmusConfig().has_operator_fee = true`，费用模型仍 Fjord
- 外部 oracle：op-revm `l1block.rs:276-283` `tx_cost` 的 ISTHMUS 臂叠加
  `operator_fee_charge`；`:184-201` operator fee 公式在 JOVIAN 换臂（product 语义）。
- 结论：Isthmus **引入 operator fee 维度** → 独立维度 → 建格，已排进 Task 5
  （`hasOperatorFee` / `hasJovianOperatorFormula` 两列）。
- 影响：Task 5 isthmus/jovian/karst 三行；无新增 finding。

### 格 3：F3/Karst 费用
- 本仓取值：`karstConfig()` 费用模型 Fjord、operator fee 延续 Jovian 公式
- 外部 oracle：op-revm `l1block.rs` 全文 KARST 在费用路径 0 命中
  （KARST 只在 spec.rs/precompiles.rs）；op-geth（e8800cffe，含 KarstTime）同样无 IsKarst 费用臂。
- 结论：Karst **不改变费用臂** → `—`；Karst 的独立维度在预编译上限（Task 6）与
  EIP-7823 三长度（Task 7）。
- 影响：Task 5 karst 行 `feeModel=Fjord, hasJovianOperatorFormula=true`；无新增 finding。

### 格 4：F4/Regolith 存款
- 本仓取值：EL 侧存款执行不区分 Regolith（payload 里的 deposit tx 按统一规则执行）
- 外部 oracle：specs `deposits.md:192-234,256-257` —— Regolith 只改**收据语义**
  （depositNonce、实际 gas、isSystemTx=false、gasLimit 150M→1G），不改 deposits-only
  处理，更不改 L1 attributes 布局（op-node `l1_block_info.go` 无 Regolith 布局分支）。
- 结论：本仓 EL 视角 **无独立维度** → `—`；收据语义格归 Plan B（夹具层）。
- 影响：无新增 finding。

### 格 5：F4/Canyon 存款
- 外部 oracle：specs `deposits.md:231-232` —— Canyon 只强制收据 API 携带
  depositNonce/depositNonceVersion 字段；L1 attributes 布局不变。
- 结论：`—`（Canyon 的实质维度是 payload 的 withdrawals，归 M2 形状基线 Task 4）。
- 影响：无新增 finding。

### 格 6：F4/Ecotone 存款
- 外部 oracle：op-node `l1_block_info.go:518-523` —— Ecotone 起 L1Info 布局追加
  BlobBaseFeeScalar/BaseFeeScalar（v4 布局），L1 attributes 存款内容随之改变。
- 结论：独立维度（L1 attributes 形状）→ 建格，但**执行层/夹具层**属性，归 Plan B；
  本计划 `—`。
- 影响：Plan B 的逐 fork meta/L1Info 形状格；无新增 finding。

### 格 7：F4/Fjord 存款
- 外部 oracle：op-node `l1_block_info.go:31-32` 布局常量只有
  `L1InfoIsthmusLen`/`L1InfoJovianLen` 两个后继档 —— Fjord 不改 L1Info 布局
  （Fjord 只改费用公式，属 F3）；DA footprint scalar 是 Jovian 加的（:32）。
- 结论：`—`。
- 影响：无新增 finding。

### 格 8：F4/Granite 存款
- 外部 oracle：`l1_block_info.go` 无 Granite 布局分支；specs deposits.md 无 Granite 条目。
- 结论：`—`（Granite 的 operator fee 参数只进 SystemConfig，不进 attributes 布局）。
- 影响：无新增 finding。

### 格 9：F4/Holocene 存款
- 外部 oracle：op-node `derive/attributes.go:218-220` —— Holocene 起 attributes 请求
  携带 `EIP1559Params`（8 字节，来自 sysConfig），L1 attributes 存款内容改变。
- 结论：独立维度 → 建格归 Plan B；本计划 `—`。
- 影响：Plan B 逐 fork meta 格；无新增 finding。

### 格 10：Regolith 的 evmc 档位
- 本仓取值：`regolithConfig().rev = EVMC_LONDON`（OpForkSchedule.cpp:98）
- 外部 oracle：op-revm 20.0.0 `src/spec.rs:36-48` → `BEDROCK|REGOLITH => SpecId::MERGE`；
  evmc 名 `EVMC_PARIS`（`evmc.h:1020`）。op-revm 口径**适用**（Bedrock/Regolith 上线晚于
  The Merge，其 EVM 语义即 Paris；不存在「Bedrock 无 Paris」的解释空间）。
- 结论：**登记偏离，不改代码**。理由：把 rev 提到 PARIS 是 engine 语义变更
  （evmone 会按 Paris 放开 LONDON 后的门控行为），牵动全部既有断言与链上兼容性，
  超出测试计划授权，需独立评审立项；当前无生产缺陷证据，且 evmone 对 LONDON 的
  解释在 Regolith 语义上是**过松**方向（风险方向已知）。
- 影响：Task 5 regolith 行钉当前值 `EVMC_LONDON` 并在注释点名偏离（规格原文即如此）；
  finding F-A1 立案（见 design-revision-pending）。

### 格 11：Karst 激活块 gas limit
- 结论：**已定性（规格先行取证）**：EL 角色**无需实现** —— 本仓
  `OpEngineService.cpp:161-165/287` 与 op-geth `consensus/beacon/consensus.go:261`
  同为 2^63-1 上限，`:398` 直接采用 CL 给的 gas limit，逐点一致。
- 待决策项（内建 driver）：`resolveDriverGasLimit` + ledger SystemConfig 要么实现
  「激活块 +upgradeGas / 下一块 strip / `KeepKarstUpgradeGas` opt-out」（op-node
  `derive/attributes.go:160-206`、`derive/payload_util.go:56-77`、`types.go:142-149`），
  要么显式登记为「仅内建 driver 模式的已知偏离」。**定夺：登记为已知偏离，不实现** ——
  实现前置是 op-node 的 `UpgradeGas(forks.Karst)` 表，本仓没有该产物，且内建 driver
  的 Karst 组合当前无 e2e 覆盖（实现将无从验证）。
- 影响：finding F-A2 立案（design-revision-pending）；Task 10 §11.2 条目按此改写。

### 格 12：Lagoon 的上游地位
- 本仓取值：解析器对未知 fork 名 **fail-closed**（`forkFromName` →
  `throwInvalidOpForkSchedule("unknown fork")`，OpForkSchedule.cpp:21-29）；
  全仓 grep `lagoon` 0 命中 —— 即本仓**没有**设计 §3 M1 所述的「容忍格」实现。
- 外部 oracle：op-node `5f90f749ca` `rollup/types.go:151-153` `LagoonTime *uint64`
  （"an experimental feature-set, activated like a hardfork"）—— 上游已把它建模为
  受控 fork 时间字段。
- 结论：**维持 fail-closed，不建容忍格**。理由：设计里的「容忍未知 fork」在本仓从未
  存在，实现它等于为未实现的 EL 语义开口子（过松方向）；Lagoon 尚无 EL 语义定稿，
  正确姿势是等上游定稿后像 Karst 一样作为真实 fork 落地；解析器拒绝即发现，行为安全。
- 影响：设计 §3 M1 的 lagoon 格改写为「上游已建模、本仓明确不建模、解析器 fail-closed」
  （design-revision-pending）；finding F-A3（设计前提失真：容忍格从未实现）。

## 汇总

- `—`（本计划无独立维度）：格 1/3/4/5/7/8
- 建格（已排进 Task 5）：格 2
- 建格（归 Plan B）：格 6/9
- 定夺（登记偏离/finding）：格 10（F-A1）、11（F-A2）、12（F-A3）
