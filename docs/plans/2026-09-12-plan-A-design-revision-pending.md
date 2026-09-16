# 设计文档修订稿（待人工合入 karst-on-5550）—— Plan A Task 1 Step 3 / Task 10

> **为什么是"pending"**：设计文档 `2026-09-12-opstack-fork-test-plan-design.md` 与
> `.agents/reviews/INDEX-OPFEAT.md` 位于只读 worktree `karst-on-5550`（用户硬约束：
> 该树不得改动）。本文件是按 M0 台账结论拟好的修订内容，需人工合入原文件。
> 本文件 untracked，永不入库。

## 一、设计文档 §3（M0 的 9 个 `·` 格改写）

| 格 | 原状态 | 新状态 | 依据 |
|---|---|---|---|
| F3/Granite 费用 | `·` | `—`（Granite 不改费用臂；回归由 M7 granite 行钉） | op-geth rollup_cost.go 无 IsGranite 费用门；op-revm l1block.rs 无 GRANITE 臂 |
| F3/Isthmus 费用 | `·` | `✓`（Task 5 建格：has_operator_fee / has_jovian_operator_formula） | op-revm l1block.rs:276-283 ISTHMUS 臂 operator_fee_charge |
| F3/Karst 费用 | `·` | `—`（Karst 独立维度在预编译/EIP-7823，Task 6/7） | l1block.rs KARST 费用路径 0 命中 |
| F4/Regolith 存款 | `·` | `—`（Regolith 只改收据语义，归 Plan B） | deposits.md:192-234,256-257 |
| F4/Canyon 存款 | `·` | `—`（收据 API 字段；实质维度是 payload withdrawals，归 M2） | deposits.md:231-232 |
| F4/Ecotone 存款 | `·` | `—`→Plan B（L1Info v4 布局加 blob scalars，夹具层格） | op-node l1_block_info.go:518-523 |
| F4/Fjord 存款 | `·` | `—`（Fjord 不改 L1Info 布局；DA scalar 是 Jovian） | l1_block_info.go:31-32 |
| F4/Granite 存款 | `·` | `—` | l1_block_info.go 无 Granite 分支 |
| F4/Holocene 存款 | `·` | `—`→Plan B（attributes 请求携带 EIP1559Params） | op-node attributes.go:218-220 |

## 二、第 10/11/12 格定夺（必须落设计文档或 finding 台账）

1. **Regolith `rev`**（格 10）→ op-revm 口径适用，本仓 `EVMC_LONDON` vs oracle `EVMC_PARIS`
   为真实偏离；**登记偏离、不改代码**（提档是语义变更，需独立评审立项）。
2. **Karst 激活块 gas limit**（格 11）→ EL 角色无需实现（与 op-geth 逐点一致）；
   **内建 driver 登记为已知偏离、不实现**（前置 op-node `UpgradeGas(forks.Karst)` 表本仓缺失）。
3. **Lagoon**（格 12）→ 上游（op-node `types.go:151-153`）已建模为受控 fork 时间字段；
   本仓解析器 fail-closed 且**从未存在容忍格**；**维持不建模、不建容忍格**，待上游
   EL 语义定稿后按 Karst 模式落地。

## 三、finding 台账（拟合入 INDEX-OPFEAT.md，Plan A/WI-01 名下）

- **F-A1（confirmed-debt / 行为偏离）** Regolith `rev=EVMC_LONDON` 语义无依据：
  op-revm 20.0.0 `spec.rs:36-48` 映射 `REGOLITH→SpecId::MERGE`（EVMC_PARIS）。本仓取值
  过松（放开 LONDON 后门控），依赖 evmone 实现兜底，无 Bedrock 口径解释空间。
  状态：偏离已登记（Task 5 regolith 行钉值点名）；**提档修复需独立评审**。
- **F-A2（deferred / 已登记偏离）** engine 层缺 `blobGasUsed == 本地重算 daFootprint`
  等值校验（op-geth `core/block_validator.go:128` 有）：本仓 `OpEngineService.cpp:180-199`
  仅有范围校验（uint64 / pre-Jovian=0 / ≤gasLimit）。**定性：未被 header hash 自洽比对
  兜住** —— header 的 blobGasUsed 来自 payload 原样透传（`OpEngineService.cpp:423`），
  无本地重算来源，比对不成立即静默接受。需在 Plan B/D 引入本地 daFootprint 重算后闭合。
- **F-A3（design-correction）** 设计 §3 M1 的「lagoon 容忍格」从未实现：解析器对未知
  fork 名 fail-closed（OpForkSchedule.cpp:21-29），全仓 0 命中。定夺为维持 fail-closed，
  §3 条目按上面第二节第 3 条改写。
- **F-A4（finding / 待裁定）** golden 的 op-geth pin 三处不一致：
  `opstack-executor/tests/support/GoldenSample.h:22-23` = `e8800cffe`；
  `t8n/.t8n-pin` = `759a9af0`；本机 op-geth 检出 `/Users/octopus/octo/code/op-geth`
  HEAD = `d0734fd5`（另 `/Users/octopus/octo/code/blockchain-impl/op-geth` = `e8800cffe`，
  与 GoldenSample.h 一致）。需裁定权威 pin；建议以 `e8800cffe`（corpus 生成源、可复现）
  为准，`.t8n-pin` 属过期文件应删除或对齐。
- **F-A5（test-gap）** OP 侧首错顺序无测试固定：契约在
  `engine/bcos-engine/OpEngineService.cpp:337-361` 注释（transactions → withdrawals →
  blobVersionedHashes → windowFields → headerFields → blobGasUsed，window 先于 header），
  无断言；Eth 侧有（`EthEngineServiceParityTest.cpp:949`）。归 Plan B 负向格。

## 四、§6 cannot-determine 条目改写

- M5 karst 行（原 `cannot-determine`）：WI-01 后**可裁决** —— op-revm 20.0.0
  `precompiles.rs` 的 Karst 常量已提取为可执行 oracle（`bcos-evm/test/opstack/op_revm_oracle.json`，
  由 `tools/op-revm-oracle/extract.sh` 生成，Task 6）；删除该 cannot-determine 条目，
  改为引用 oracle 契约。若 `p256verify_gas` 未能从本机 registry 解析，则最小读取集 =
  `revm-precompile <ver>/src/secp256r1.rs`，条目改写为该最小读取集的 cannot-determine。

## 五、§11.2 第 1 条（内建 driver）改写

「Karst 激活块 gas limit：EL 角色无需实现（OpEngineService 与 op-geth 逐点一致）；
内建 driver 登记为已知偏离（F-A2 同族），实现前置 = op-node `UpgradeGas(forks.Karst)`
表入库 + 内建 driver Karst e2e 覆盖，二者均缺失，暂不排期。」
