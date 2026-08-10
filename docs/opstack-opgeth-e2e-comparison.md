# FISCO opstack ↔ op-geth 块执行端到端对比方案

> 目标：从 RPC 入口触发，对 FISCO-BCOS OP Stack 执行器与 op-geth 的**区块执行流程**做端到端逐阶段对比，输出差异矩阵 + 差分门禁 + 端到端真链对拍方法。
> 生成日期：2026-08-07。依据：三个探索 agent（FISCO 侧链路 / op-geth 侧链路 / 既有基建盘点）+ 本地代码核实。
> **2026-08-07 复审**：4 个审查 agent 对全文锚点做了严格代码对比（每点附 file:line + 源码摘录），更正清单见 **§0.0**。op-geth 锚点已全部复核到权威版本 v1.101702.2。

---

## §0.0 审查更正清单（2026-08-07，4 个审查 agent 证据级复核）

> 本节为对正文的**权威更正**，与正文冲突时以本节为准。证据均已由审查 agent 逐点 `file:line + 源码摘录` 核实。

### 高优先（反转/结构性）

| # | 正文声明 | 更正 | 证据 |
|---|---|---|---|
| C-1 | **§0.2/§0.3 版本事实**：v1.101702.2 无 NewPayloadV5/SlotNumber/Amsterdam（rc.3 独有），rc.3 较新 | **错误**。v1.101702.2 三者都有（`eth/catalyst/api.go:770-794` NewPayloadV5、`beacon/engine/types.go:120/72/369` SlotNumber、`params/config.go:506/959` Amsterdam）；且 **rc.3 是 v1.101702.2 的祖先**（`git merge-base --is-ancestor d0734fd5f e8800cffe`=YES）。真实版本差异仅：(a) v1.101702.2 新增 `PostExecTxType`(0x4B) 使 `case DepositTxType` 下移 2 行（:228-229）；(b) catalyst api.go 整体 +124 行。**Amsterdam 两版都有，FISCO 无对应，对比仍停在 Jovian/Isthmus** | agent② |
| C-2 | **B-5 / §1 阶段2**：FISCO 侧无 DA footprint 块级实现，"仅作非共识 meta" | **反转**。`feat-op-validator-loop` 已实现全部三处：① header `blobGasUsed=Σ meta.da_footprint`（`OpBlockSeal.cpp:74-80`，承诺比对 `EngineServiceImpl.h:1130-1135`，写头 `EngineServiceImpl.cpp:495`）；② ==且≤GasLimit 校验（`EngineServiceImpl.cpp:442-444`）；③ 下块 base fee `max(parent.gasUsed, parent.blobGasUsed)`（`EngineServiceImpl.cpp:233-239 calcOpBaseFee`，头校验 `EngineServiceImpl.h:945`）。B-5 应改为**"已实现，待与 op-geth golden 对拍"**，删除"需块级 PR 补齐" | agent③ |
| C-3 | **§0.1**：opstack-executor/ 是 `.git/info/exclude` 忽略的参考碎片 | **错误**。`opstack-executor/` 是 index 中 tracked-unmerged（DU），gitignore 不适用于已跟踪文件；`.git/info/exclude` 无 `opstack-executor` 条目（grep=0）。`bcos-evm/bcos-evm/opstack/` 也仅逐文件忽略 `OpBlock.h/cpp`，非整目录 | agent① |
| C-4 | **§4 缺口B**：`EngineServiceInitializer::build<OpSchedulerImpl>(maxEngineVersion=4)` | **`build` 无 `maxEngineVersion` 参数**，末参为 `blockTxCountLimit`（默认 `c_defaultBlockTxCountLimit`）。装配需以实际签名改写 | agent① |
| C-5 | **§1 阶段3**：opTransition "fee 路由至 vaults :47-53" | **:47-53 是 EIP-7702 recovery 段**；实际 fee 路由至 vaults 在 `OpTransition.cpp:186-191`（OP_BASE_FEE_VAULT :186、OP_L1_FEE_VAULT :188、OP_OPERATOR_FEE_VAULT :190-191） | agent③ |
| C-6 | **B-6 括号标签**："slot8[18,20)" | slot8 是 `OperatorFeeParamsSlot`（operatorFeeScalar/Constant）；`da_footprint_gas_scalar` 只从首笔 L1 attributes 的 **calldata[176:178]** 提取（`rollup_cost.go:547-557 ExtractDAFootprintGasScalar`），不读任何槽。**主结论"不读槽(calldata 提取)"成立**；FISCO `OpBlockExecute.cpp:130-152` 同法提取 | agent③ |

### 中优先（行号/路径修正）

| # | 正文声明 | 更正 | 证据 |
|---|---|---|---|
| C-7 | §1 阶段1 op-geth 行号：NewPayloadV1-V4 :569-635、newPayload :672、Namespace :58 | v1.101702.2：NewPayloadV1 **:695**/V2 **:703**/V3 **:724**/V4 **:743**（+126）；newPayload **:796**（+124）；Namespace 注册 **:56**。`checkOptimismPayload:12` 不变 | agent② |
| C-8 | §1 阶段0 `decodeOneRawTx(OpSchedulerImpl.h:1026)` | **:1026 是 executeOpBlock 内调用点**；函数定义在 `OpSchedulerImpl.h:857`，分派逻辑 :858-890（legacy≥0xc0/0x7e/0x01/0x02/0x04） | agent② |
| C-9 | §1 阶段3/4 op-geth 行号偏移（非语义差异） | `innerExecute` :515、Regolith 退款 :681-689、OP fee 分发 :711-734（BaseFeeRecipient :719 / L1FeeRecipient :725 / OperatorFeeRecipient :732 / refundIsthmusOperatorCost 调用 :729）、`refundIsthmusOperatorCost` 定义 :836、`MakeReceipt` :199、`FinalizeAndAssemble` :383、Isthmus withdrawalsRoot :416-427、Jovian BlobGasUsed :429-437、`writeBlockWithState` :1650 | agent③ |
| C-10 | §4 缺口B：`EngineServiceImpl.h:193-194 c_opMode`、`EngineServiceInitializer.h:27-28` | 实际 **:200-201**（c_opMode SFINAE）、**:19-20**（模板） | agent① |
| C-11 | §4 缺口A：`EngineServiceImpl.h:494` 消费 withdrawalsRoot | 文件归属错，实际 **`EngineServiceImpl.cpp:494`** `header->setWithdrawalsRoot(...)`（.h 无 setWithdrawalsRoot） | agent① |
| C-12 | §1 阶段3 `evm.go "OperatorCostFunc :70-72/:89"` | :89 实为 `L1CostFunc`，operator 无 :89 副本 | agent③ |
| C-13 | §2 结构差异"FISCO 无独立预校验阶段" | 表述过强。`feat-op-validator-loop` 已有 `validateOpNewPayloadRequest`（`EngineServiceImpl.cpp:279`，含 rawTransactions:299 / withdrawalsRoot:318 / blobGasUsed:324-334 / DA≤gasLimit:442-444）与 base fee 静态校验（`EngineServiceImpl.h:945`）；缺的只是完整 VerifyHeader/ValidateBody 等价物，承诺比对仍是执行后 | agent③ |
| C-14 | §1 阶段5 `bcos-ledger/LedgerMethods.h:233-235` | 路径应为 `bcos-ledger/bcos-ledger/LedgerMethods.h`；行号仅对 pr-5366 成立（val-loop 偏移 2 行） | agent④ |

### 已证实（无需改，供留档）

- §0.1 基线（pr-5366@bec4afa7e 无 OP 代码）、DU 冲突 stage1=1830118bb/stage3=e80949e2、include 缺失头、两分支 EngineHelper 均不填 rawTransactions/withdrawalsRoot、op-geth 双 checkout 版本、§4 缺口 A/C 锚点（validateOpNewPayloadRequest :279/:299/:318、executeBlock throw 哑桩）、**B-1/B-2/B-3/B-4/B-7/D-3 均代码级确认**（withdrawalsRoot 结构一致、receipt 编码逐字段吻合、OP 回执字段 post-Ecotone 全对齐、blockGasLeft 递减+cumulative 回填、系统调用先于首笔、DA footprint 不进 tx 级）。
- §3/§5 全部计数（33 向量、16+17、golden/engine 41 文件、OpDepositEncodeTest 4、OpBlockExecuteTest 14、OpBlockSealTest 13、EEST G1-G8、build-allocs.py）证实。仅 mutation matrix 实际 20 个 TEST（18=spec §7.3 矩阵 + 2 个 batch C 增补，agent④）。
- B-2 可升级为"代码级已确认一致"，保留 golden 逐位确认。B-8 维持"待 L1/L2 对拍"。
- ⚠️ **现场变化**：审查期间仓库已从 pr-5366 切到 `feat-op-executor-core@482b931e6`（OP 模块现可在工作区看到）；"当前 checkout 无 OP 代码"仅对基线时点成立。

---

## 0. 基线锁定（一切对比的前提）

⚠️ **三处基线事实必须先钉死，否则对比口径全乱：**

### 0.1 当前 checkout 没有 OP 代码
- 当前分支 `pr-5366`（HEAD `bec4afa7e`）**未合入任何 OP 实现**。工作区里的 `opstack-executor/` 是 **index 中 tracked-unmerged（DU）冲突文件**（gitignore 不适用，非"参考碎片"，见 §0.0 C-3）；`bcos-evm/bcos-evm/opstack/` 仅 `OpBlock.h/cpp` 两个文件被 `.git/info/exclude` 逐文件忽略（非整目录）：
  - `opstack-executor/OpstackExecutor.h` + `tests/OpstackExecutorTest.cpp` 处于 **DU（deleted-by-us）未合并冲突态**（stage1=1830118bb、stage3=e80949e2），且其 include 的 `OpTransition.h`/`OpFeeParams.h`/`OpForkSchedule.h` 等工作区缺失 → **当前 checkout 无法编译这些 OP 文件**。
  - `EngineHelper.cpp` 的 `rawTransactions`/`withdrawalsRoot` 改动、`TransactionReceiptImpl` 的 `.empty()` 版等记忆 #27/#28 声称的"工作区改动"**当前均不存在**。
  - ⚠️ 审查期间仓库已切到 `feat-op-executor-core@482b931e6`，此节描述仅对 `pr-5366@bec4afa7e` 基线成立。

**→ FISCO 侧权威基线分支**（按完整度）：
| 分支 | OP 文件数 | 定位 |
|---|---|---|
| `feat-op-validator-loop` | 188 | **最完整**，与记忆锚点全吻合（EngineHelper/withdrawalsRoot/registerOpBlock/OpSchedulerImpl/完整 opstack 模块 + 10 测试） |
| `feat-op-t8n-corpus` | 145 | t8n 语料 + `bcos-evm-opstack-tests`（16 源文件） |
| `feat-op-executor-core` | 47 | `engineApiForV1Only` 门控所在；rawTransactions/withdrawalsRoot 载体 |
| `feat-opstack-executor` / `feat-op-protocol-rpc` | 37/34 | 模块/协议专项 |

### 0.2 op-geth 有两个 checkout，版本不同
| checkout | 版本 | 角色 |
|---|---|---|
| `~/octo/code/blockchain-impl/op-geth` | **v1.101702.2** @ `e8800cffe` | **权威参考** —— t8n generator 生产金值的库，与记忆对标版本一致 |
| `~/octo/code/op-geth` | v1.101701.0-rc.3 @ `d0734fd5f` | 同代快照（**两版均含** Amsterdam `NewPayloadV5`/header `SlotNumber`）；**不是金标准生产者** |

**版本差异影响对比口径**（经审查复核，见 §0.0 C-1）：
- **两个 checkout 都没有** `engine_newPayloadV2/V3Deposits` 独立端点 → deposits **内联在 `Transactions` 数组**（首笔 L1 attributes deposit，0x7E）。
- v1.101702.2 与 rc.3 在 NewPayloadV5/SlotNumber/Amsterdam 上**无差异**（rc.3 是 v1.101702.2 的祖先）。真实差异仅：(a) v1.101702.2 新增 `PostExecTxType`(0x4B) 使 `case DepositTxType` 下移 2 行（`transaction.go:228-229`）；(b) catalyst `api.go` 整体 +124 行。
- **Amsterdam 两版都有，FISCO 无对应 → 对比停在 Jovian/Isthmus**。
- 本方案正文 op-geth 锚点已由 4 个审查 agent **复核到 v1.101702.2**（行号迁移见 §0.0）。

### 0.3 已完成的交易级审计（不要重做）
`opstack-opgeth-parity-audit`（记忆）已对 **v1.101702.2** 做过逐点审计：费用预扣/refund/L1+operator 公式/FlzCompressLen(deposit 全套)/EIP-7702/intrinsic+7623/空账户删除/CREATE 地址/receipt 共识编码 **逐位等价**。唯一确认缺口 = **Karst 占位**（`OpForkSchedule.cpp:93-101` karstConfig 仅是 jovianConfig 别名）。

---

## 1. 端到端阶段划分（双端锚点映射）

从 `engine_newPayload` 触发，共 7 个阶段。FISCO 侧锚点以 `feat-op-validator-loop` 为权威；op-geth 侧锚点已复核到 v1.101702.2（行号迁移见 §0.0 C-7/C-9）。

### 阶段 0 — 数据形态（raw bytes ↔ 对象）
| FISCO | op-geth |
|---|---|
| `ExecutionPayload`（`bcos-framework/bcos-framework/engine/Types.h:89-130`，含 `rawTransactions`:128、`withdrawalsRoot`:129） | `ExecutableData.Transactions`（`beacon/engine/types.go:252` `DecodeTransactions` → `tx.UnmarshalBinary`）；deposits 内联 |
| `decodeOneRawTx`（定义 `OpSchedulerImpl.h:857`，分派 :858-890 EIP-2718 legacy/0x7E/0x01/0x02/0x04；executeOpBlock 调用点 :1025-1026） | `Transaction.decodeTyped`（`core/types/transaction.go:212`，`case DepositTxType` 0x7E 在 :228-229） |
| `bcosTransactionToEvmone` 宽松转换（丢严格校验） | `core/types/deposit_tx.go:27-46`（SourceHash/From/To/Mint/Value/Gas/IsSystemTransaction/Data；nonce≡0、gasPrice≡0） |

**差异点**：FISCO 双端都要做三次类型翻译（OpBlockTx→protocol::Transaction→evmone）；op-geth 一次 raw→types.Transaction。**FISCO 侧 `rawTransactions` 未接 RPC**（见 §4 缺口A）。

### 阶段 1 — RPC 入口
| FISCO | op-geth |
|---|---|
| 注册：`EndpointsMapping.cpp:68-71`（engine_newPayloadV1..V4）；V4 现为 UnsupportedFork 桩 | 注册：`cmd/geth/config.go:351` + `eth/catalyst/api.go:56` Namespace（NewPayloadV1 **:695**/V2 **:703**/V3 **:724**/V4 **:743**/V5 :770） |
| 分派：`EngineEndpoint.cpp:177-221` → `parseNewPayloadRequest` | 唯一路径：`api.go:796` `newPayload` → `checkOptimismPayload`（`api_optimism.go:12`：Canyon 后 withdrawals 必须空、extraData 校验、Isthmus 后 withdrawalsRoot 非空） |
| OP 校验：`EngineServiceImpl.cpp:279` `validateOpNewPayloadRequest`；:299 rawTransactions 必须、:318 withdrawalsRoot 必须 | `ExecutableDataToBlockNoHash`（`beacon/engine/types.go:289`，Isthmus withdrawalsRoot 校验 :320-327） |

**差异点**：op-geth 的 OP 校验在 catalyst 层（checkOptimismPayload + EIP-2718 解码）；FISCO 的 OP 校验在 `validateOpNewPayloadRequest`。**FISCO 侧 rawTransactions/withdrawalsRoot 因缺口A 从 RPC 进来必被静态校验拒绝。**

### 阶段 2 — 块校验（header/extraData/base fee/DA footprint）
| FISCO | op-geth |
|---|---|
| （FISCO 侧块校验分散，主要靠六/八项承诺比对：receiptsRoot/logsBloom/withdrawalsRoot/stateRoot/gasUsed/txRoot/blobGasUsed/requestsHash，`EngineServiceImpl.h` OP 分支） | `BlockValidator.ValidateBody`（`core/block_validator.go:51`；Isthmus no-withdrawal + withdrawalsRoot 移入 ValidateState :80-87;190-198；OP 跳过 blob 校验 :109-117） |
| | **Jovian DA footprint 块级限制**：`block_validator.go:119-134`（`CalcDAFootprint` 必须等于且 ≤ gasLimit） |
| | extraData：`eip1559.ValidateOptimismExtraData`（`eip1559_optimism.go:22`）三套编码（Bedrock 空 / Holocene / Jovian v1） |
| | base fee：`eip1559.CalcBaseFee`（`eip1559.go:64`；Jovian `calcBaseFeeInner` :99-107 用 `max(parentGasUsed, parentDAFootprint)`） |

**差异点**：op-geth 的块校验完整（header 级 + 状态级，VerifyHeader/ValidateBody）；FISCO 有 `validateOpNewPayloadRequest`（`EngineServiceImpl.cpp:279`）+ base fee 静态校验（`EngineServiceImpl.h:945 calcOpBaseFee`），但**主体仍是执行后的"六/八项承诺比对"**，无完整 VerifyHeader/ValidateBody 等价物（见 §0.0 C-13）。Jovian DA footprint 块级逻辑**两边均已实现**（见 §0.0 C-2：FISCO header 写 + ==且≤GasLimit + 下块 base fee max），非 op-geth 独有。

### 阶段 3 — 状态执行（核心）
| FISCO | op-geth |
|---|---|
| 执行器两套并存：**A** `OpstackExecutor.h`（v2 本地编辑，未装配）/ **B** `OpSchedulerImpl.h:1016 executeOpBlock`（六步：decode→bridge→processOpBlock→poison 检查→seal+stateRoot→txRoot） | `ProcessBlock`（`core/blockchain.go:2086`）→ `StateProcessor.Process`（`state_processor.go:62`，预执行系统调用 beaconRoot :90-92 / parentHash :93-95） |
| `processOpBlock`（`OpBlockExecute.cpp:75`）：首笔必须 L1 attributes deposit :85-91；逐笔 runDeposit/opValidate/opTransition；`blockGasLeft -= gas_used` :115/:165 | `ApplyMessage`/`innerExecute`（`state_transition.go:515`）→ `buyGas`（:282，预扣 gasLimit*gasPrice **叠加 L1+operator cost** :288-308） |
| `opValidate`（`OpValidate.cpp:7`）：拒 blob、validate_transaction、L1/operator 费用预扣、balance<maxCost 拒绝 :40-41 | `preCheck` deposit 分支（`state_transition.go:346-361`：不查 nonce/fee/EOA，gas 免费，系统交易不占 gas pool） |
| `opTransition`（`OpTransition.cpp:143`）：evmone executeMessage、**fee 路由至 vaults :186-191**（OP_BASE_FEE_VAULT :186、OP_L1_FEE_VAULT :188、OP_OPERATOR_FEE_VAULT :190-191） | `execute` mint + 失败 deposit 递增 nonce（:473-511）；Regolith 退款不打 coinbase（:681-689）；**OP fee 分发三目标**（base→BaseFeeRecipient :719、L1→L1FeeRecipient :725、operator→OperatorFeeRecipient :732，:711-734）；`refundIsthmusOperatorCost`（定义 :836-846） |
| `runDeposit`（`OpDepositTx.cpp:58`）：0x7E + mint | `NewL1CostFunc`/`NewOperatorCostFunc`（`rollup_cost.go:151,215,353`，Bedrock/Ecotone/Fjord/Isthmus/Jovian 多版本）；`CalcDAFootprint`（:563） |
| validate/transition 两阶段用**同一次 fee 快照**（记忆：#3 契约） | 两阶段**即时读**（buyGas 预扣 → innerExecute 分发） |

**差异点**：
1. FISCO `opValidate` 与 `opTransition` 是**两个独立阶段共享 fee 快照**；op-geth `buyGas` 与 `innerExecute` 同一 stateTransition 对象**即时读**。跨块复用/两阶段读不同状态会分叉（记忆 #3，建议加断言+测试固化）。
2. FISCO 用 `loadOpFeeParams`（`OpBlockExecute.cpp:128`）惰性加载；op-geth 从 L1Block 预部署合约存储槽即时解析。
3. **Jovian DA footprint 不进 FISCO tx 级 gas_used/cumulative/receiptsRoot/stateRoot**（记忆已核实与 op-geth 一致，纯块级 header 字段）。

### 阶段 4 — 块级收尾（建根）
| FISCO | op-geth |
|---|---|
| `computeOpTxRoot`（`OpEngineSeam.h:171`）：`evmone::state::MPT`，key=rlp(index)，leaf=raw tx 字节 | `Header.Hash()`（`core/types/block.go`）：RLP keccak；`NewBlock` DeriveSha 算 TxHash/ReceiptHash |
| `sealOpBlock`（`OpBlockSeal.cpp:29`）：receiptsRoot（MPT over rlp(i)+encodeReceiptForRoot）、logsBloom 按位或、**Isthmus+ withdrawalsRoot=opStorageRoot(messagePasserStorage)** :60-64、requestsHash、**Jovian blobGasUsed=Σ meta.da_footprint** :74-80 | `Beacon.FinalizeAndAssemble`（`consensus/beacon/consensus.go:383`）：定 stateRoot；Isthmus `withdrawalsRoot=statedb.GetStorageRoot(L2ToL1MessagePasser)` :416-427；Jovian 写 `CalcDAFootprint` 进 header.BlobGasUsed :429-437 |
| `stateRootOf`（`StateRootCompute.h:83`）：全状态 MPT，account key=keccak(addr) | `statedb.Commit`（`core/blockchain.go:1681-1697`） |
| `mapOpReceipt`（`OpReceiptMap.h:120`）：createReceipt + setEffectiveGasPrice + setOpStackMeta | `MakeReceipt`（`state_processor.go:199`）+ `depositReceiptRLP`（`receipt.go:136-148`：追加 DepositNonce/DepositReceiptVersion）+ `deriveOPStackFields`（`receipt_opstack.go:11`：L1GasPrice/L1BlobBaseFee/L1GasUsed/L1Fee/scalars） |

**差异点**：
1. **withdrawalsRoot 语义一致**（都是 L2ToL1MessagePasser storage root），但 FISCO 用 `opStorageRoot`（secure-trie over storage map），op-geth 用 `GetStorageRoot`——**需逐位对拍**。
2. receiptsRoot 均用 MPT over rlp(i)+receipt 编码——**FISCO `encodeReceiptForRoot` 是否与 op-geth `depositReceiptRLP`/`receiptRLP` 逐位等价需再验证**（记忆称共识编码逐位等价，但那是 tx 级）。
3. op-geth 的 OP 回执扩展字段（L1GasPrice/L1Fee/scalars/DAFootprintGasScalar）由 `DeriveFields`（`receipt.go:596`）在落库前派生；FISCO 的 `setOpStackMeta` 在 `mapOpReceipt` 同步做。**字段集合与编码需对拍**（记忆 `op-gettransactionreceipt-parity` 已发现 OP 扩展字段全缺 → 已补齐 3 字段）。

### 阶段 5 — 落库 / 索引
| FISCO | op-geth |
|---|---|
| `registerOpBlock`（`EngineServiceImpl.h:1200`）：SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER / SYS_HASH_2_RECEIPT / **SYS_HASH_2_TX**（方案 B：`opEnvelopeToTars` 转换后写 tars Transaction，`EngineServiceImpl.h:1293-1307`）；s_eth_hash_2_rawtx 写已删（:1253-1254）；0x04/损坏信封转换失败返回 nullopt 跳过写表（D7） | `writeBlockWithState`（`core/blockchain.go:1650`）：`rawdb.WriteBlock/WriteReceipts`（:1664-1665）→ 通用 `eth/tracers`/rpc 统一查询 |
| 已知边界：`bcos-ledger/bcos-ledger/LedgerMethods.h:233-235` 对缺失 SYS_HASH_2_TX 行无 has_value 检查直接解引用 → UB（pre-existing；行号仅对 pr-5366 成立，val-loop 偏移 2 行） | — |

**差异点**：OP 交易经方案 B（2026-08-10）落通用 SYS_HASH_2_TX（`opEnvelopeToTars` 转换后写，key=keccak 信封 hash），读侧 eth_getTransactionByHash / eth_getTransactionReceipt 与普通交易同通道可查；s_eth_hash_2_rawtx 写已删。残留 divergence：0x04 (EIP-7702) 无 handler → 跳过写表，读侧返回 null（块执行不受影响，D7）。

### 阶段 6 — 输出（block hash / output root）
| FISCO | op-geth |
|---|---|
| block hash 由承诺比对与 payload 校验保证（op-geth 自己的 `block.Hash()` 用于 gate 测试） | block hash = `Header.Hash()` RLP keccak；payload 的 BlockHash 在 `beacon/engine/types.go:280-282` 核对 |
| **L2 output root：FISCO 无对应实现**（不在此对比范围） | **op-geth 不算 output root**，由 op-node 计算：`ComputeL2OutputRootV0`（`optimism/op-node/rollup/output_root.go:29`，输入 Version/StateRoot/MessagePasserStorageRoot/BlockHash → `op-service/eth/output.go:45-67` 拼 128 字节再 keccak） |

**差异点**：output root 在 OP 架构里是 op-node 的职责（CL 侧），FISCO 若要提供对等能力需在 CL/consensus 层补，**不在 EL 执行器对比范围**。FISCO 的 gate 测试已用 `block.Hash()` 对齐。

---

## 2. 已知差异清单（汇总，按"待确认/已确认"分类）

> **完整差异矩阵见** `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（W4，L0 静态对拍）。
> **B 项最新状态**：

| B 项 | 状态 |
|---|---|
| B-1 | 已修一致 |
| B-2 | 事实达成 |
| B-3 | 已确认（动态 manifest 待 W5） |
| B-4 | 事实达成 |
| B-5a | 事实达成 |
| B-5b | 待W5 |
| B-5c | 待W5 |
| B-6 | 已确认 |
| B-7 | 待W5（仅顺序可观测性） |
| B-8 | 已修一致 |

### 已确认（不要重做）
| # | 差异 | 状态 |
|---|---|---|
| D-1 | 交易级执行（费用/L1+operator/Flz/deposit/7702/intrinsic/空账户/CREATE 地址/receipt 共识编码）逐位等价 | ✅ 记忆审计（v1.101702.2） |
| D-2 | **Karst 占位**：karstConfig 仅 jovianConfig 别名 | 🔴 上线 Karst 前必须按真实 diff 适配 |
| D-3 | Jovian DA footprint 是纯块级 header 字段，不进 tx 级状态 | ✅ 已核实（tx 级一致） |
| D-4 | validate↔transition 费用快照契约：FISCO 两阶段共享快照 vs op-geth 即时读 | ⚠️ 依赖上层一致；需断言+测试固化 |

### 本对比要新确认的（块级重点）
| # | 对比项 | 锚点 | 方法 |
|---|---|---|---|
| B-1 | withdrawalsRoot = L2ToL1MessagePasser storage root 逐位一致（opStorageRoot vs GetStorageRoot） | FISCO `OpBlockSeal.cpp:60-64` / op-geth `consensus.go:416-427` | 结构已代码级确认一致（secure-trie、keccak key、value trim+RLP、零槽移除）；**golden 逐位对拍收尾** |
| B-2 | receiptsRoot 编码（FISCO `encodeReceiptForRoot` vs op-geth `receiptRLP`/`depositReceiptRLP`） | `OpBlockSeal.cpp:29` / `receipt.go:128-148` | **已代码级确认字段/顺序/status 编码逐字段一致**（deposit 与 normal 均吻合）；golden 逐位确认收尾 |
| B-3 | OP 回执扩展字段集合与编码（L1GasPrice/L1Fee/scalars/DAFootprintGasScalar） | `OpReceiptMap.h:120` / `receipt_opstack.go:11` | 代码级已确认 post-Ecotone 字段全对齐；**2 处 delta**：op-geth legacy `FeeScalar`（pre-Ecotone）FISCO 无；FISCO `operator_fee`（实际扣费额）op-geth 回执无 |
| B-4 | blockGasLeft 逐笔递减与 cumulative_gas_used 回填契约 | `OpBlockExecute.cpp:115-117/:165-167` | 已代码级确认（deposit/normal 均递减+回填） |
| B-5 | DA footprint 三处块级：header BlobGasUsed=CalcDAFootprint、块校验 ==且≤GasLimit、下块 base fee=max(parentGasUsed,parentDAFootprint) | op-geth `rollup_cost.go:563` / `block_validator.go:119-134` / `eip1559.go:99-107` | **FISCO 已实现三处**（见 §0.0 C-2：`OpBlockSeal.cpp:74-80` / `EngineServiceImpl.cpp:442-444` / `calcOpBaseFee` :233-239）→ **golden 对拍** |
| B-6 | da_footprint_gas_scalar 读取方式 | `rollup_cost.go:547-557 ExtractDAFootprintGasScalar` | **已确认**：仅从首笔 L1 attributes **calldata[176:178]** 提取，不读任何槽（slot8 是 `OperatorFeeParamsSlot`，非 DA scalar）；FISCO `OpBlockExecute.cpp:130-152` 同法 |
| B-7 | 系统调用顺序（beaconRoot/parentHash 预执行） | `state_processor.go:90-95` / `OpBlockExecute.cpp:78-82` | 结构已确认先于首笔；逐字节金标准仍需 op-geth 输出对拍 |
| B-8 | 链式双块（区块间 state 延续、跨块 fee） | golden/engine chained/ | golden chainA/B |

### 结构性差异（FISCO 架构层面）
- **块校验位置**：op-geth 有独立 header/body 预校验（ValidateBody/verifyHeader + EIP-1559 header 校验）；FISCO 有 `validateOpNewPayloadRequest`（`EngineServiceImpl.cpp:279`）+ base fee 静态校验（`EngineServiceImpl.h:945`），但主体仍是执行后"六/八项承诺比对"，无完整 VerifyHeader/ValidateBody 等价物（见 §0.0 C-13）。
- **双执行器并存**：`OpstackExecutor.h`（v2 未装配）与 `OpSchedulerImpl::executeOpBlock`（`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:1016`，生产未装配）两套；op-geth 只有一条路径。
- ~~**索引隔离**~~：方案 B（2026-08-10）已消除——OP 交易经 `opEnvelopeToTars` 转换后写 SYS_HASH_2_TX（`EngineServiceImpl.h:1293-1307`），读侧统一可查；s_eth_hash_2_rawtx 写删。残留 divergence：0x04 读侧 null。
- **PBFT 双执行防护**：`OpSchedulerImpl::executeBlock` throw 哑桩（`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:987` 定义 / :993 throw），仅在 OP scheduler 装配后生效。

---

## 3. 对比方法（三层）

### L0 — 静态源码对拍
按 §1 的 7 阶段清单逐项核对双端实现。**已有**：交易级审计（D-1）。**本方案新增**：块级 8 项（B-1~B-8）逐项落锚点 + 判定。产出：阶段差异矩阵（§5）。

### L1 — 动态差分 gate（已有基建，直接复用）
| 组件 | 位置 | 现状 |
|---|---|---|
| t8n 块级向量 + 回放器 | `bcos-evm/test/opstack/t8n/vectors/*.json`（33）+ `OpT8nReplayTest.cpp` | 首轮 25/25 向量、2943 次比对 0 分歧；33 向量全量回放 `processOpBlock→sealOpBlock`，六字段 + postState 双向 + 写集覆盖 |
| engine 级 golden gate | `feat-op-validator-loop:EngineNewPayloadGateTest.cpp`（33 向量 + 链式双块 + 13 类 18 例 mutation matrix） | 在分支，main tree 无 |
| golden 语料 | `feat-op-validator-loop:bcos-evm/test/opstack/t8n/golden/engine/`（41 文件，含 `chained/chainA/B.golden.json`、`SHA256SUMS`） | 在分支 |
| EEST OP 适配器 | `.claude/worktrees/opstack-eest-adapter/bcos-evm/specs-tests/`（OpStackEest*Runner + skip-list G1-G8） | worktree |

**关键动作**：把 B-1~B-8 中的 FISCO 可执行项（B-1/B-2/B-3/B-4/B-7/B-8）**加进 t8n 向量集的六字段比对断言**（当前只比 tx 级六字段 + postState；需把 withdrawalsRoot/storage-root 相关纳入）。golden 用 `t8n/generator/main.go`（op-geth 当库 `GenerateChainWithGenesis`+`InsertChain`）在 `blockchain-impl/op-geth`（v1.101702.2）确定性重放。

### L2 — 端到端真链对拍（前置两个硬缺口）
驱动**真实 `engine_newPayload` RPC** → FISCO RPC 层 → OP 执行器 → 落库，与 op-geth 金标准对拍：
- 输入：真实 RPC payload（raw EIP-2718 bytes，deposit + 普通 tx 混合，取自 golden/engine 或自建链式块）。
- 断言：blockHash / stateRoot / receiptsRoot / withdrawalsRoot / gasUsed / txRoot / logsBloom 七项全等。
- 形态：`EngineNewPayloadGateTest` 从"测试内手工构造 NewPayloadRequest"升级为"真实 RPC 请求 → EngineHelper.parse → EngineService"。

**当前阻塞**：缺口A（RPC 不填 rawTransactions/withdrawalsRoot）+ 缺口B（composition root 未装配）——见 §4。

---

## 4. 前置硬缺口（端到端对拍的前置条件）

| 缺口 | 现状 | 修复 | 对应记忆任务 |
|---|---|---|---|
| **A. RPC rawTransactions/withdrawalsRoot 未填** | `EngineHelper.cpp parseNewPayloadRequest`（feat-op-executor-core 与 val-loop 均如此）只填 `payload.transactions`，从不填 `rawTransactions`（原始 EIP-2718 字节）和 `withdrawalsRoot` → OP 路径从真实 RPC 进来**必被 `validateOpNewPayloadRequest`（:299/:318）静态拒绝** | 在 `parseNewPayloadRequest` 中：解析 `transactions` 时同时保留原始 hex 字节 → `payload.rawTransactions`；解析 `withdrawalsRoot` → `payload.withdrawalsRoot`。注意 `EngineServiceImpl.cpp:494` 已消费 `withdrawalsRoot` | **#27**（记忆称"已写待验"——**实际未写，需重做**） |
| **B. Composition root 未装配 OpSchedulerImpl** | 生产 `Initializer.cpp` 传 baseline `SchedulerParallelImpl`(:311)/`SchedulerSerialImpl`(:329) → `EngineServiceImpl::c_opMode`（`EngineServiceImpl.h:200-201` SFINAE）恒 false，OP 分支编译期关闭；`OpSchedulerImpl` 只在测试实例化 | 加 `OPSTACK_EXECUTOR_VERSION` 常量 + `opStackMode` 门控，装配 `OpSchedulerImpl`（⚠️ **`EngineServiceInitializer::build` 无 `maxEngineVersion` 参数**，末参为 `blockTxCountLimit`，见 §0.0 C-4；`EngineServiceInitializer.h:19-20` 模板）；`engineApiForV1Only`（仅 feat-op-executor-core:348）随之定版 | **#28**（方案C 已定） |
| **C. PBFT 双执行门控** | `OpSchedulerImpl::executeBlock` throw 哑桩存在但未生效（因 B 未装配） | OP 模式禁用 PBFT executeBlock；双执行防护从"哑桩 throw"升级为门控 | **#29** |
| **D. 版本/分支决策** | 当前 checkout 无 OP 代码；需选定 FISCO 基线（建议 `feat-op-validator-loop`）与 op-geth 参考（`blockchain-impl/op-geth` v1.101702.2） | 先合并/切到目标分支，锁定版本，再开始 L2 | 前置 |

> ⚠️ 记忆 #27 更正：记忆记录"`EngineHelper.cpp`(rawTransactions + withdrawalsRoot) 已修待验"，但当前所有相关分支的工作区/分支代码**均未填**这两个字段。**该任务实为未完成**，L2 对拍前必须重做。

---

## 5. 产出物

1. **阶段差异矩阵**（§1 表格化）：7 阶段 × 双端锚点 × 差异判定 × 待确认标记。机器可读版本可并入 `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`。
2. **块级差分 gate 扩充**：B-1~B-8 全部落入 t8n 向量断言或新增向量；`DIVERGENCES.md` 增加块级台账（M-B 系列）。
3. **端到端对拍报告**：L2 真链对拍的七项断言结果表。
4. **对比结论**：每阶段"等价 / 已知分叉 / 结构性差异"三态，及上线 Karst 前必办清单。

---

## 6. 建议执行顺序

```
Phase 0  基线决策（缺口D）: 定 FISCO 基线分支 + op-geth v1.101702.2 复核锚点
Phase 1  缺口A: EngineHelper 填 rawTransactions/withdrawalsRoot + RPC 层测试   ← 记忆#27 重做
Phase 2  缺口B+C: composition root 装配 OpSchedulerImpl + PBFT 门控           ← 记忆#28/#29
Phase 3  L0 静态对拍: 按 §1 清单核对 7 阶段, 补 B-1~B-8 锚点判定             → 阶段差异矩阵
Phase 4  L1 差分 gate: 块级断言扩进 t8n 向量 + DIVERGENCES.md 块级台账
Phase 5  L2 端到端: 真实 newPayload RPC → OP 执行器 → 七项断言 vs op-geth golden
Phase 6  结论: 差异矩阵定稿 + Karst 上线闸清单
```

**最小可交付（先做 Phase 0→2 → Phase 4 跑通 33 向量块级断言）**：即便不做 L2 真链，L1 块级差分 gate + 静态矩阵已能覆盖"从 RPC 入口的逐阶段差异"，L2 是验证接线正确性的最后一环。

---

## §7 L2 端到端真链对拍报告（W6，2026-08-07）

> **形态**：进程内 RPC 对拍——真实 JSON params → `EngineHelper::parseNewPayloadRequest(V4)` → `EngineService<OpSchedulerImpl>.newPayload(4)` → `executeOpBlock`（落库 + 承诺比对）→ 七项断言。
> **语料**：`t8n/vectors/`（33，pre-state）+ `t8n/golden/engine/`（41 文件 = 33 golden + chained/ 6 + `SHA256SUMS`/`manifest.txt`），op-geth **v1.101702.2** 确定性生成。
> **断言**：七项（blockHash=keccak256(encodeOpHeader)、stateRoot、receiptsRoot、withdrawalsRoot、gasUsed、txRoot、logsBloom）+ **encodeOpHeader 字节级全等**（`OpNewPayloadRpcE2eTest.cpp:177-205` `assertSevenFields`）。
> **执行**：`OpNewPayloadRpcE2eSuite`（`bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp`，commit `16e747e91`），**34 用例 = 33 向量 + 1 链式对（chainA/B）**，实测 2026-08-10 全绿。

### 结果表（33 向量 + 链式双块，全 PASS）

| 向量 | fork | VALID | 七项全等 | 备注 |
|---|---|---|---|---|
| isthmus_access_list | isthmus | ✅ | ✅ | — |
| isthmus_big_block_130tx | isthmus | ✅ | ✅ | **B-8 类修复验证**：txRoot ≥128 笔排序（旧 `5e8b0395…` → golden `f8477d27…`） |
| isthmus_contract_create | isthmus | ✅ | ✅ | — |
| isthmus_contract_logs | isthmus | ✅ | ✅ | — |
| isthmus_deposit_failed | isthmus | ✅ | ✅ | — |
| isthmus_deposit_mint | isthmus | ✅ | ✅ | — |
| isthmus_deposit_only | isthmus | ✅ | ✅ | — |
| isthmus_empty_account_cleanup | isthmus | ✅ | ✅ | — |
| isthmus_fee_env_observer | isthmus | ✅ | ✅ | — |
| isthmus_message_passer_write | isthmus | ✅ | ✅ | **B-1 修复验证**：opStorageRoot leaf 二次 RLP（旧 `d00be84d…` → golden `02dffd0c…`） |
| isthmus_setcode_7702 | isthmus | ✅ | ✅ | — |
| isthmus_setcode_7702_skips | isthmus | ✅ | ✅ | — |
| isthmus_system_contracts_real | isthmus | ✅ | ✅ | — |
| isthmus_transfer_basic | isthmus | ✅ | ✅ | — |
| isthmus_transfer_multi | isthmus | ✅ | ✅ | — |
| isthmus_tx_reverted | isthmus | ✅ | ✅ | — |
| jovian_access_list | jovian | ✅ | ✅ | — |
| jovian_contract_create | jovian | ✅ | ✅ | — |
| jovian_contract_logs | jovian | ✅ | ✅ | — |
| jovian_da_mix | jovian | ✅ | ✅ | — |
| jovian_deposit_failed | jovian | ✅ | ✅ | — |
| jovian_deposit_mint | jovian | ✅ | ✅ | — |
| jovian_deposit_only | jovian | ✅ | ✅ | — |
| jovian_empty_account_cleanup | jovian | ✅ | ✅ | — |
| jovian_fee_env_observer | jovian | ✅ | ✅ | — |
| jovian_first_block | jovian | ✅ | ✅ | — |
| jovian_message_passer_write | jovian | ✅ | ✅ | **B-1 修复验证**：opStorageRoot leaf 二次 RLP（旧 `d00be84d…` → golden `02dffd0c…`） |
| jovian_setcode_7702 | jovian | ✅ | ✅ | — |
| jovian_setcode_7702_skips | jovian | ✅ | ✅ | — |
| jovian_system_contracts_real | jovian | ✅ | ✅ | — |
| jovian_transfer_basic | jovian | ✅ | ✅ | — |
| jovian_transfer_multi | jovian | ✅ | ✅ | — |
| jovian_tx_reverted | jovian | ✅ | ✅ | — |
| **chainA** | isthmus | ✅ | ✅ | 块 1（受信创世 parent 预登记） |
| **chainB** | isthmus | ✅ | ✅ | 块 2（parent=chainA；先投 SYNCING → A 落库后 VALID） |

**汇总**：34/34 全 PASS。每向量 `VALID` + 七项 + `encodeOpHeader` 字节级全等；链式双块验证区块间 state 延续与跨块 fee。全量回归：**OP 145/145、ledger 186 全绿**。

### 差异归因（3 个真实 parity 分歧，W6 实测抓出并修复）

#### 分歧 1 — `opStorageRoot` leaf value 二次 RLP（B-1）

- **根因**：op-geth 的 storage-trie leaf value = **`rlp(trimmed value)`**（`trie/secure_trie.go` `UpdateStorage`: `v, _ := rlp.EncodeToBytes(value)` 后再入 trie），即 leaf 内是 rlp(trim) 的字节串，相对 raw trim 是**二次 RLP**。FISCO `opStorageRoot` 旧实现把 raw trim 直接当 leaf value，单槽时根哈希偏离。
- **暴露向量**：`isthmus_message_passer_write` / `jovian_message_passer_write`（单槽 message passer 写），旧 FISCO withdrawalsRoot `d00be84d…` ≠ golden `02dffd0c9faf74ea1d45a209c7ceac479f92927e204d2398e61a238f7357760d`。
- **修复**（`bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:67-97`，修复注释 :82-91）：先对 trim 值 rlp 编码成 leaf 字节串，再入 secure-trie——与 `accountStorageRoot`（`adapter/StateRootCompute.cpp:21-22`，stateRoot 据此匹配 golden）对齐。
- **影响**：**B-1 从「待 golden 逐位对拍收尾」→「已修一致」**。withdrawalsRoot 现与 op-geth golden 逐位匹配（isthmus/jovian 两向量均验证）。§2 B-1 锚点因修复由 `OpBlockSeal.cpp:60-64` 移至 `:67-97`。

#### 分歧 2 — `computeTrieRootVarKey` ≥128 笔排序 bug（B-8 类 txRoot 大块）

- **根因**：`hbBuild` REQUIRES entries 按 nibble-path 序（first/last common-prefix shortcut），但文档化 caller 契约「input sorted by caller」被两个 OP 调用方（`computeOpTxRoot` in `OpEngineSeam.h:171`、`sealOpBlock` in `OpBlockSeal.cpp`）违反——传 **index 序**。小笔数碰巧正确（index 0 的 key `0x80` 不与 2 字节 key 共享首 nibble）；**≥128 笔时** `rlp(128..)=0x8180..` 与 `0x80` 共享 `0x81` nibble-prefix，middle entries（`0x01..0x7f`）从 0-7 起 → first/last 公共前缀退化为 `[8]`，产出**畸形 extension node**。
- **暴露向量**：`isthmus_big_block_130tx`（131 笔），旧 FISCO txRoot `5e8b0395…` ≠ op-geth `DeriveSha` `f8477d27fa1f5a6d1176fd00122fd1e24b4a0b20c4eb2d93e188817173ef7ecb`。
- **修复**（`bcos-ledger/bcos-ledger/mpt/HashBuilder.cpp:200-230`，排序 :222-230）：`computeTrieRootVarKey` 内**防御性排序**（byte 字典序 == nibble-path 序），API 对 caller 序不再敏感。
- **影响**：**txRoot 大块（B-8 类）已修一致**。排序内聚到 API 内后，两个 OP 调用方的顺序契约坑被消除。

#### 分歧 3 — `EngineHelper` decode typed-catch 在 evmone(-fno-rtti) 下失效（RTTI 现象）

- **根因**：本二进制链接 evmone（`-fno-rtti`），tars 的 `TarsDecodeMismatch`（`runtime_error` 子树）typeinfo 缺失，typed `catch(std::exception const&)` 无法可靠绑定 → 解码异常逃逸/误判。与 `bcos-evm/OpSchedulerImpl.h` `executeOpBlock` 的 `catch(...)` 注释记录同一 RTTI 现象。
- **暴露**：W6 L2 harness 直编本文件即暴露（decode 容错失败，影响全部向量的 decode 路径）。
- **修复**（`bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp:81-90`）：改 `catch(...)`——严格更强，贴合「跳过失败笔」意图。
- **影响**：全部 33 向量的 decode 容错稳定；不改变任何已通过向量的输出，属健壮性修复。

### §2 待确认项更新（被 T4 改变的项）

| # | §2 原状态 | §7 实测后状态 |
|---|---|---|
| B-1 | 结构已代码级确认一致（secure-trie、keccak key、value trim+RLP、零槽移除）；**golden 逐位对拍收尾** | **golden 逐位已对拍一致**——opStorageRoot leaf 二次 RLP 修复（分歧 1），`message_passer_write`（isthmus+jovian）两向量验证 |
| B-8 | **golden chainA/B**（待对拍） | **L2 已对拍一致**——chainA/B 链式双块七项全等（state 延续 + 跨块 fee）；B-8 类 txRoot 大块由分歧 2 修复（`isthmus_big_block_130tx`）覆盖 |

> 附（非正式状态迁移）：B-2（receiptsRoot）/B-4（gasUsed 回填）亦随七项断言在全部 33 向量逐位匹配，golden 收尾事实上达成；正式状态迁移留待 W7 结论定稿。B-3/B-5/B-6/B-7 维持 §2 状态。

### W6 外待办（记入）

- **V4 端点桩**：`newPayloadV4` RPC 端点实现（生产 op-node 互通时修；**C1 决策 2026-08-10：端点补全前不广告 V4**）
- **PBFT retry loop**：OP 模式 proposal 短路后无限重推（禁 sealer/抑制重推决策）
- **V4 能力广播**：~~广告 V4 实为正确~~ **已降级（2026-08-10，C1，commit 124f105）**：`supportedOpCapabilities` 不再追加 V4（端点未注册,op-node 协商即 -38005 撞桩）；V4 端点补全后恢复广告
- ~~**generator 重生成 golden**~~：**已就绪-按需执行（W8 T5 验证 `regen.sh` exit 0 可复现）**——触发条件（op-geth 版本变更/新向量/新 fork）未发生，无需动作；执行需 op-geth v1.101702.2 环境（PIN e8800cffe）

---

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
| 4 块级收尾 | 等价 + 1 已知分叉 | B 台账终态已定（B-1/B-8 已修一致；B-2/B-4/B-5a 事实达成）+ B-3 回执扩展字段 2 delta |
| 5 落库 | 等价 | OP 交易落通用 SYS_HASH_2_TX（方案 B）；0x04 读侧 null 注记 |
| 6 输出 | 等价 | block hash 承诺比对；output root 不在 EL 范围 |

### 已知差异明细落定

- **D 项终态**：D-1/D-3 等价（已确认）；D-2 Karst 🔴（阻塞，论证见上线闸）；D-4 已知分叉（契约已固化）
- **B 项终态**：B-1/B-8 已修一致；B-2/B-4/B-5a 事实达成（B-2/B-4 正式迁移在此落定）；B-3 已知分叉（2 delta，注记收紧归 W7）；B-5b/B-5c/B-6/B-7 已确认
- **结构性差异「接受」决策**：
  - 块校验位置：接受（承诺比对是 FISCO 架构选择，VALID/INVALID 语义互通无碍）
  - 双执行器并存：接受（v2 未装配，生产单路径）
  - **索引隔离：已消除（方案 B，2026-08-10）**——OP 交易经 `opEnvelopeToTars` 转换后写 SYS_HASH_2_TX，读侧 eth_getTransactionReceipt / eth_getTransactionByHash 与普通交易同通道可查；s_eth_hash_2_rawtx 写删。残留：0x04 (EIP-7702) 读侧 null（TransactionType 无 handler，块仍 VALID）
  - PBFT 哑桩：接受（W3 门控已生效），但与「PBFT 共识层是否整体禁用」未决决策纠缠——纯 EL 无碍，自持共识上线需决策

### Karst 上线闸

1. **gap 清单**（影响 / 工作量 / 阻塞性）：

| gap | 影响 | 工作量 | 阻塞性 |
|---|---|---|---|
| D-2 Karst 适配 | ~~高——FISCO 无 karstTime 激活通道…~~ **用户裁定 2026-08-10：不处理**（op-geth 侧 Karst 纯 config 骨架、真实内容在 op-reth 且生态已要求迁移，无对拍对象） | 中高 | ~~🔴 阻塞~~ → **已关闭** |
| ~~OP 块回执不可查~~ | **已修复（方案 B，2026-08-10）**——OP 交易转换后写 SYS_HASH_2_TX + 读侧 opStackMeta 13 字段输出；eth_getTransactionReceipt / eth_getTransactionByHash 与普通交易同通道可查 | — | 🔴 ~~生产阻塞~~ → 已关闭 |
| PBFT 共识层未决 | 中——自持共识上线阻塞；纯 EL 视角可降级互通项 | 中 | 视上线形态 |
| B-2/B-4 正式迁移 | 低（W7 内完成） | 低 | 否 |
| B-3 注记收紧 | 低（W7 内完成） | 低 | 否 |
| deferred minors（cases/ gitignore、golden manifest 校验、首投 B 软断言） | 低 | 低 | 否 |

2. **修复排期**（⛔ 用户裁定 2026-08-10 移除 Karst 专项）：
```
（Karst 适配已裁定不处理）→ OP 块回执可查修复（剩余 🔴 项）→ 可上线评估
```
3. **Go/No-Go**：~~**当前 No-Go**——FISCO 无法激活/表征 Karst…~~ **更新（2026-08-10）：Karst 阻塞已关闭**（用户裁定不处理）**+ OP 块回执不可查已修复**（方案 B：交易写 SYS_HASH_2_TX + 读侧 opStackMeta 字段输出）。剩余 🔴 = **无**。重新评估可上线的条件收敛为：① 方案 B 回归通过（W5 gate + test-bcos-rpc + bcos-evm-opstack-tests 全绿）② PBFT 共识层决策（§8.5，视上线形态）③ 按需重新对拍（不含 Karst）。

### 待办移交（W7 之后）

- ~~**Karst 适配专项任务**~~（**用户裁定 2026-08-10：不处理**——op-geth 侧 Karst 是纯 config 骨架（`IsKarst` 零行为调用点，registry commit cc07e96d9 无任何链配 karst_time），真实 Karst 内容（Fusaka 7 EIP / BN256 上限 / L2CM）在 op-reth 侧且 OP 生态已要求 Karst 后迁移 op-reth；FISCO 对拍基线停在 Jovian/Isthmus，Karst 无对拍对象）
- ~~**OP 块回执可查修复**~~（**方案 B 已完成，2026-08-10**——交易经 `opEnvelopeToTars` 转换写 SYS_HASH_2_TX + 读侧 opStackMeta 字段输出；采用方案 B，未走 val-loop 的 rawtx 回退方案）
- **PBFT 共识层决策**（是否整体禁用 + retry loop 抑制）
- deferred minors 清理（cases/ gitignore、golden manifest 校验、首投 B 软断言）
- 生产互通待办（**V4 能力广播已降级**（C1，commit 124f105）/ **V4 端点桩**仍待办 / generator 重生成——见 §7「W6 外待办」）
- **W8 / W0**（记忆遗留 ctest/落盘/四项决策 + DU 冲突清理）
- **s_number_2_header 落盘欠账**（W8 T2 核实确认）：OP 提交路径 `EngineServiceImpl.h:1167` 只 `pushView`、从不 `mergeBackStorage()` → 同 view 内所有 OP 写表（s_number_2_header / SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / 回执表 / SYS_HASH_2_TX（方案 B））只存内存层，不落后端 RocksDB，进程重启即丢。最小 loop 未接真实节点故暂留档（落盘无消费方）；修复应随 orchestration 层接真实节点时整层 merge（仿 FISCO `:662-669`，采纳原子 mergeView 规避 throw 泄漏），与 `SYS_CURRENT_STATE` head 推进/reorg 编排同批。核实报告见 SDD workspace `task-2-report.md`。
- **legacy/0x01 块执行放行留档**（W8 C6 裁定确认）：FISCO OP 块执行层对 legacy(0xc0)/access_list(0x01) **放行** = 与 op-geth **等价**（`OpSchedulerImpl.h` decodeOneRawTx 分派无拒绝分支 + `OpTransition.cpp` opValidate 白名单仅拒 blob 0x03/>0x04；op-geth `state_transition.go` 块执行层亦仅 IsDepositTx 特殊分支）；**不实现硬拒**——若硬拒会对合法区块单侧判 INVALID，与差分对拍目标冲突。裁定报告见 SDD workspace `task-4-report.md`。

> 注：B-2/B-4 正式迁移 + B-3 注记收紧已在 §8.3 W7 内完成，不入此移交。
