# ANCHOR-CORRECTIONS.md — 校正后锚点表（W4 L0 差异矩阵底稿）

> **用途**：W4 L0 静态对拍差异矩阵（Task 2-4）的锚点底稿。替换 `docs/opstack-opgeth-e2e-comparison.md` §1 中已过时/错位的锚点。
>
> **校正值来源**：W4 审查 R1/R2（4-agent 证据级复核，含 off-by-1 修正）。本文件每行均已在**当前分支**实际 grep/Read 核实（2026-08-10）。
>
> **核实基准**：FISCO 侧 = 本分支 `feat-op-executor-e2e`；op-geth 侧 = `~/octo/code/blockchain-impl/op-geth`（`v1.101702.2`，git describe 确认）。
>
> 表格式：`| 阶段 | 锚点（§1 原） | 校正后 FISCO 锚点 | 校正后 op-geth 锚点 | 校正依据 |`
> 依据列 = 「W4 审查 R1/R2 + 本次 grep/Read 核实」（列本次核实证据：grep 命令命中行或 Read 区段）。

---

## 表 A — FISCO 侧校正锚点（核本分支）

| 阶段 | 锚点（§1 原） | 校正后 FISCO 锚点 | 校正后 op-geth 锚点（对照） | 校正依据 |
|---|---|---|---|---|
| 0 数据形态 | `OpSchedulerImpl.h:857` `decodeOneRawTx` | **`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:855`**（定义；executeOpBlock 调用点 :1023） | `Transaction.decodeTyped`（`core/types/transaction.go:212`） | W4 审查 R1/R2 + grep `decodeOneRawTx` 命中 :855 |
| 0 数据形态 | `bcosTransactionToEvmone`（OP 锚点误） | **OP 路径 = `bcos-evm/bcos-evm/opstack/OpTransition.cpp:456-457`**（`evmone::state::Transaction tx;` 直接构造，type=legacy :457） | `Transaction.UnmarshalBinary`/`decodeTyped`（`core/types/transaction.go:212`，deposit 内联） | W4 审查 R1/R2 + grep `evmone::state::Transaction` 命中 :456-457；`bcosTransactionToEvmone` 非 OP 路径（见「实质过时」#2） |
| 2 块校验 | `EngineServiceImpl.cpp:233-239` | **def `engine/bcos-engine/EngineServiceImpl.cpp:191`**（`calcOpBaseFee` 定义）；Jovian max 逻辑 :233-240（`parentIsJovian && parent.blobGasUsed>gasUsed` 取 max，:236-239） | `eip1559.CalcBaseFee`（`eip1559.go:64`）；Jovian `calcBaseFeeInner` `:99-107` | W4 审查 R1/R2 + grep `calcOpBaseFee` 命中 :191；Read :225-249 确认 :233-240 |
| 3 状态执行 | `OpValidate.cpp:7`（**文件不存在**） | **`bcos-evm/bcos-evm/opstack/OpTransition.cpp:328`**（`opValidate`） | `preCheck` deposit 分支（`state_transition.go:346-361`） | W4 审查 R1/R2 + grep `opValidate` 命中 :328；`opstack/` 目录无 `OpValidate.cpp` |
| 3 状态执行 | `OpDepositTx.cpp:58`（**文件不存在**） | **`bcos-evm/bcos-evm/opstack/OpTransition.cpp:441`**（`runDeposit`；OpBlockExecute.cpp:139 调用） | `core/types/deposit_tx.go:27-46`；`NewL1CostFunc`/`NewOperatorCostFunc`（`rollup_cost.go:151,215,353`） | W4 审查 R1/R2 + grep `runDeposit` 命中 :441；`opstack/` 目录无 `OpDepositTx.cpp` |
| 3 状态执行 | `OpBlockExecute.cpp:75` `processOpBlock` | **`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:99`** | `ProcessBlock`（`core/blockchain.go:2121` 校正）→ `StateProcessor.Process`（`state_processor.go:62`） | W4 审查 R1/R2 + grep `processOpBlock` 命中 :99 |
| 3 状态执行 | `OpBlockExecute.cpp:85-91` 首笔 deposit | **`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:112-116`**（empty-block 拒 :112-113；首笔必须 L1 attributes deposit :114-116） | `checkOptimismPayload`（`api_optimism.go:12`，Isthmus deposits-only） | W4 审查 R1/R2 + Read :105-189 确认 :112-116 |
| 3 状态执行 | `OpBlockExecute.cpp:115/:165` `blockGasLeft` | **`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:143/:198`**（deposit 分支 :143 / 普通 tx 分支 :198，`blockGasLeft -= gasUsed`） | `buyGas`（`state_transition.go:282`，预扣 gasLimit*gasPrice 叠加 L1+operator :288-308） | W4 审查 R1/R2 + Read :105-212 确认 :143/:198 |
| 3 状态执行 | `OpBlockExecute.cpp:128` `loadOpFeeParams` | **`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:157`**（惰性加载，feeLoaded 守卫 :152） | `NewL1CostFunc`/`NewOperatorCostFunc`（`rollup_cost.go:151,215,353`） | W4 审查 R1/R2 + grep `loadOpFeeParams` 命中 :157 |
| 3 状态执行 | `OpBlockExecute.cpp:130-152` DA calldata | **`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:164-180`**；`JovianL1AttributesLen=178`（OpBlockExecute.h:66），**提取在 :176-178**（`attrData[176]<<8 | attrData[177]` = calldata[176:178]；W4-B off-by-1 修正后固定偏移） | `ExtractDAFootprintGasScalar`（`rollup_cost.go:555`）；`CalcDAFootprint:571-577`（激活块 len==176 强制 0） | W4 审查 R1/R2 + Read :164-180 确认；常量 grep `JovianL1AttributesLen=178`（OpBlockExecute.h:66） |
| 3 状态执行 | `OpTransition.cpp:143/:186-191` `opTransition`/vault | **`bcos-evm/bcos-evm/opstack/OpTransition.cpp:237`**（`opTransition` 定义）；**vault 路由 :295-300**（OP_BASE_FEE_VAULT :295、OP_L1_FEE_VAULT :297、OP_OPERATOR_FEE_VAULT :300） | `execute` OP fee 分发三目标（base→BaseFeeRecipient :719、L1→L1FeeRecipient :725、operator→OperatorFeeRecipient :732，:711-734）；`refundIsthmusOperatorCost` :836-846 | W4 审查 R1/R2 + grep `opTransition\|OP_BASE_FEE_VAULT` 命中 :237/:295；Read :290-304 确认 vault 路由 |
| 4 块级收尾 | `OpReceiptMap.h:120`（**文件不存在**） | **`bcos-evm/bcos-evm/opstack/OpTransition.cpp:318-321`**（`makeFiscoReceipt` :318 调用 + `setOpStackMeta` :319；`:144` 为 makeFiscoReceipt 定义；mapOpReceipt 区） | `MakeReceipt`（`core/state_processor.go:199`）；`receipt_opstack.go:11` `deriveOPStackFields` | W4 审查 R1/R2 + grep `makeFiscoReceipt\|setOpStackMeta` 命中 :144/:318-319/:519-526；`opstack/` 目录无 `OpReceiptMap.h` |
| 4 块级收尾 | `OpBlockSeal.cpp:29` `sealOpBlock` | **`bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:138`** | `Beacon.FinalizeAndAssemble`（`consensus/beacon/consensus.go:383`） | W4 审查 R1/R2 + grep `sealOpBlock` 命中 :138 |
| 4 块级收尾 | `OpBlockSeal.cpp:60-64` withdrawalsRoot | **`bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:170-174`**（Isthmus 分支 :170，`seal.withdrawalsRoot = opStorageRoot(messagePasserStorage)` :172，requestsHash :173） | Isthmus `withdrawalsRoot=statedb.GetStorageRoot(L2ToL1MessagePasser)`（`consensus.go:416-427`） | W4 审查 R1/R2 + grep `withdrawalsRoot` 命中 :172；Read :168-181 确认调用点 :170-174 |
| 4 块级收尾 | `OpBlockSeal.cpp:74-80` blobGasUsed | **`bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:187-197`**（`cfg.has_da_footprint` :187，Σ meta.da_footprint :193-194，`seal.blobGasUsed = footprint` :196） | Jovian 写 `CalcDAFootprint` 进 header.BlobGasUsed（`consensus.go:429-437`） | W4 审查 R1/R2 + grep `blobGasUsed\|da_footprint` 命中 :185-197 |
| 4 块级收尾 | `StateRootCompute.h:83` | **`bcos-evm/bcos-evm/adapter/StateRootCompute.h:76`**（`stateRootOf`） | `statedb.Commit`（`core/blockchain.go:1681-1697`） | W4 审查 R1/R2 + grep `stateRootOf` 命中 :76 |
| 5 落库/索引 | `LedgerMethods.h:233-235` | **`bcos-ledger/bcos-ledger/LedgerMethods.h:237`**（解引用点 `auto field = txEntry->get()`；循环 :235） | `writeBlockWithState`（`core/blockchain.go:1650`） | W4 审查 R1/R2 + grep `txEntry` 命中 :235/:237 |

**FISCO 核实统计**：17 条校正锚点全部 grep/Read 命中；其中 3 条原锚点指向的文件不存在（`OpValidate.cpp`/`OpDepositTx.cpp`/`OpReceiptMap.h`，均已并入 `OpTransition.cpp`/`OpBlockExecute.cpp`）。

---

## 表 B — op-geth 侧校正锚点（核 v1.101702.2）

| 阶段 | 锚点（§1 原） | 校正后 FISCO 锚点（对照） | 校正后 op-geth 锚点 | 校正依据 |
|---|---|---|---|---|
| 3 状态执行 | `core/blockchain.go:2086` `ProcessBlock` | `processOpBlock`（`OpBlockExecute.cpp:99` 校正） | **`core/blockchain.go:2121`**（`func (bc *BlockChain) ProcessBlock(...)`；:2086 实为 `bpr.Witness()`） | W4 审查 R1/R2 + grep `func.*ProcessBlock` 命中 :2121；Read :2080-2095 确认 :2086 = `bpr.Witness()` |
| 2 块校验 | `eip1559_optimism.go:22` | `validateOpNewPayloadRequest` extraData 校验（`engine/bcos-engine/EngineServiceImpl.cpp:279`） | **`consensus/misc/eip1559/eip1559_optimism.go:22`**（`ValidateOptimismExtraData`） | W4 审查 R1/R2 + 路径补全核实 `ls` 确认文件存在 |
| 2/3 块校验/状态执行 | `rollup_cost.go` | `loadOpFeeParams`（`OpBlockExecute.cpp:157` 校正）；`RollupCost.cpp` | **`core/types/rollup_cost.go`** | W4 审查 R1/R2 + 路径补全核实 `ls` 确认文件存在 |
| 4 块级收尾 | `receipt.go:199` `MakeReceipt` | `makeFiscoReceipt`/`setOpStackMeta`（`OpTransition.cpp:318-321` 校正） | **`core/state_processor.go:199`**（`func MakeReceipt(...)`） | W4 审查 R1/R2 + grep `func MakeReceipt` 命中 `core/state_processor.go:199` |

**op-geth 核实统计**：4 条校正锚点全部命中；`git describe` 确认 checkout 为 `v1.101702.2`。

---

## 实质过时记录（供矩阵差异点用）

1. **缺口A 已修**：`bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp` `parseNewPayloadRequest` 现填 `rawTransactions` **:71-76**（`:71` `payload.rawTransactions.emplace()`、`:76` `push_back(txData)` 无条件保留，与 decode 解耦）与 `withdrawalsRoot` **:116-119**（`:118` `parseH256`）。§1 阶段 0/1 所述「FISCO 侧 rawTransactions 未接 RPC / 缺口A 静态拒绝」已过时（已核实，Read :60-119）。OP 交易不再依赖 `payload.transactions` 解码（decode 失败仅跳过，`catch(...)` 注释记录 evmone -fno-rtti 下 typed-catch 不可靠）。
2. **`bcosTransactionToEvmone` 非 OP 路径**：§1 阶段 0 用它描述 OP 宽松转换系锚点误置；OP 路径实际在 `OpTransition.cpp:456-457` 直接构造 `evmone::state::Transaction`（见表 A 阶段 0 行），`bcosTransactionToEvmone` 属以太坊 executor 通道，不在 OP 块执行链上。

---

## 核实统计汇总

| 侧 | 校正条数 | 全部命中 | 原锚点文件不存在 | 实质过时记录 |
|---|---|---|---|---|
| FISCO（本分支） | 17 | 17/17 | 3（OpValidate.cpp / OpDepositTx.cpp / OpReceiptMap.h） | 2（缺口A / bcosTransactionToEvmone） |
| op-geth（v1.101702.2） | 4 | 4/4 | — | — |
