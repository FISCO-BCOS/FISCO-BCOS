# DIVERGENCES — FISCO opstack ↔ op-geth v1.101702.2 块执行差异矩阵

> 来源：W4（L0 静态对拍）。锚点以 `ANCHOR-CORRECTIONS.md` 校正后为准。
> 判定：等价 / 已知分叉 / 结构性差异。状态：已确认 / 已修一致 / 事实达成 / 待W5。
> 覆盖：7 阶段 + 8 项 B 项（B-5 拆 a/b/c）+ 结构性差异 + D 项。

> 核实基准：FISCO 侧 = 本分支 `feat-op-executor-e2e`（worktree 根）；op-geth 侧 = `~/octo/code/blockchain-impl/op-geth`（`v1.101702.2`，git describe 确认）。本文件所有锚点已按 ANCHOR-CORRECTIONS 校正并在两侧 grep/Read 复核（2026-08-10）。op-geth 锚点路径已补全至模块前缀。

## 状态图例（§4.1 四态）

| 状态 | 含义 |
|---|---|
| 已确认 | 静态对拍已确认：双端实现存在、锚点证据级核实、路径对齐。 |
| 已修一致 | 该差异此前已存在，现已修复，双端现行为一致。 |
| 事实达成 | 双端实现路径不同，但最终事实等价（字节级/行为级已由向量佐证）。 |
| 待W5 | 需 W5 动态验证或 Jovian 链式对才能定论（含 B-5 项单侧路径）。 |

## 判定三态

| 判定 | 含义 |
|---|---|
| 等价 | 双端行为/结果一致。 |
| 已知分叉 | 双端存在已识别的行为差异，分叉点已知且有意保留。 |
| 结构性差异 | 双端架构/实现位置不同；语义可能仍等价，需逐点看证据列。 |

> 两维正交：判定三态描述「行为是否一致」，状态图例描述「证据强度 / 当前处置」。

---

## 阶段 0 矩阵（数据形态）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| ExecutionPayload | `bcos-framework/bcos-framework/engine/Types.h:89-130` | `beacon/engine/types.go:252` DecodeTransactions | 等价 | 结构一致 | 已确认 |
| typed tx 解码 | `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:855` decodeOneRawTx | `core/types/transaction.go:212` decodeTyped | 等价 | 分派覆盖 0x7E/0x01/0x02/0x04 | 已确认 |
| deposit 转换 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:456-457` | `core/types/deposit_tx.go:27-46` | 等价 | 字段映射一致 | 已确认 |
| 差异点：三次类型翻译 | — | — | 结构性差异 | FISCO 双端三次翻译 vs op-geth 一次 | 已确认 |

> **注（F-1）**：新 OP 路径（「deposit 转换」行）在 `OpTransition.cpp:456-457` 直接构造 `evmone::state::Transaction`，已不走 `bcosTransactionToEvmone`（该函数属以太坊 executor 通道，非 OP 块执行链，ANCHOR-CORRECTIONS「实质过时」#2）；但旧模块 `opstack-executor/OpstackExecutor.h:258,289` 仍复用 `bcosTransactionToEvmone`（validate :258 / execute :289），存在新、旧两条 tx 翻译路径并存——旧模块属历史 executor 通道，不参与本矩阵的阶段 0 新路径，但语义迁移时需留意。

---

## 阶段 1 矩阵（RPC 入口）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 注册 | `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.cpp:68-71` | `eth/catalyst/api.go:695/703/724/743/770` | 结构性差异 | FISCO V4 桩、V5 缺失 | 已确认 |
| 分派 | `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp:177-221` | `eth/catalyst/api.go:796` newPayload | 等价 | 唯一路径 | 已确认 |
| OP 校验 | `engine/bcos-engine/EngineServiceImpl.cpp:279/:294/:313` | `beacon/engine/types.go:289/:320-327` | 等价 | rawTransactions 必填 :294、withdrawalsRoot 必填 :313（W4-B 修正 :299/:318） | 已确认 |
| 差异点：校验位置 | validateOpNewPayloadRequest | checkOptimismPayload | 结构性差异 | 执行后 vs catalyst 层 | 已确认 |

---

## 阶段 2 矩阵（块校验）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 块校验主体 | `engine/bcos-engine/EngineServiceImpl.h:1094-1147` 承诺比对 | `core/block_validator.go:51` ValidateBody | 结构性差异 | 执行后承诺比对 vs 预校验（§2 结构性差异） | 已确认 |
| extraData | `engine/bcos-engine/EngineServiceImpl.cpp:383-421`（validateOpNewPayloadRequest 内 Isthmus 9B/Jovian 17B 形状校验） | `consensus/misc/eip1559/eip1559_optimism.go:22` | 等价 | FISCO 已实现（对齐 ValidateHolocene/JovianExtraData）；W4-C 证伪「无对应」 | 已确认 |
| base fee | `engine/bcos-engine/EngineServiceImpl.cpp:191`（def）；Jovian max 分支 :233-240 | `consensus/misc/eip1559/eip1559.go:64/:99-107` | 等价 | 一般路径经 33 向量 encodeOpHeader 字节级佐证；**Jovian max 分支已由 jovian 链式对证实（B-5c：block2 baseFee=0x3a7e98a8=calcOpBaseFee(DA)）** | 已确认 |
| DA footprint ==≤GasLimit | `engine/bcos-engine/EngineServiceImpl.cpp:442-444` | `core/block_validator.go:119-134` | 等价 | 均实现（jovian_da_mix DA=593600 字节级） | 事实达成 |
| 单侧：DA 拒绝路径 | `engine/bcos-engine/EngineServiceImpl.cpp:442-444` | `core/block_validator.go:131-132` | 结构性差异 | B-5b 拒绝向量已验证：`jovian_da_mix` 改 `blobGasUsed=gasLimit+1` → INVALID + "DA footprint"（OpL1EdgeGateTest/DAFootprintExceedsGasLimitRejected） | 已确认 |

> ⚠️ base fee 的 FISCO 锚点是 `EngineServiceImpl.cpp:191`（def）——§1 写 :233-239 是 Jovian max 逻辑，两者都要在证据列区分；Jovian max 分支状态与 B-5c 保持一致（已确认）。

---

## 阶段 3 矩阵（状态执行）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 块执行入口 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:99` processOpBlock | `core/state_processor.go:62` Process | 等价 | 首笔 deposit 强制（:112-116） | 已确认 |
| 首笔 L1 attributes | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:112-116` | `core/state_transition.go:346-361` preCheck | 等价 | D-1 交易级 | 已确认 |
| blockGasLeft 递减 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:143/:198` | `core/state_transition.go:282` buyGas | 等价 | B-4 相关 | 事实达成 |
| fee 惰性加载 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:157` loadOpFeeParams | `core/types/rollup_cost.go:151/:215/:353`（NewL1CostFunc/NewOperatorCostFunc/NewTotalRollupCostFunc） | 等价 | D-4 相关；随 D-4 快照契约固化 + 既有 golden（jovian 链式对 baseFee=calcOpBaseFee(DA)）关闭 | 已确认 |
| validate | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:328` opValidate | `core/state_transition.go:346` preCheck | 等价 | D-1 | 已确认 |
| fee 路由至 vaults | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:295-300` | `core/state_transition.go:711-734` | 等价 | C-5 校正后 | 已确认 |
| deposit 执行 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:441` runDeposit | `core/state_transition.go:473-511` | 等价 | D-1 | 已确认 |
| 差异点#1 快照契约 | `opValidate/opTransition 共享快照（OpTransition.cpp:328/:237）` | `buyGas/innerExecute 即时读（state_transition.go:282/:515）` | 已知分叉 | D-4 契约已固化（TransitionUsesValidateSnapshot：opValidate 写 F → slot1 写 F' → opTransition 用 props=F 非 F'） | 已确认 |

---

## 阶段 4 矩阵（块级收尾）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| txRoot | `bcos-evm/bcos-evm/engine/OpEngineSeam.h:171` computeOpTxRoot | `core/types/block.go:271` DeriveSha（TxHash） | 等价 | HashBuilder 排序修复 | 已修一致 |
| 密封 header | `bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:138` sealOpBlock | `consensus/beacon/consensus.go:383` FinalizeAndAssemble | 等价 | encodeOpHeader 字节级 | 事实达成 |
| withdrawalsRoot | `bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:170-174` | `consensus/beacon/consensus.go:416-427` | 等价 | B-1 | 已修一致 |
| blobGasUsed | `bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:187-197` | `consensus/beacon/consensus.go:429-437` | 等价 | B-5a | 事实达成 |
| stateRoot | `bcos-evm/bcos-evm/adapter/StateRootCompute.h:76` stateRootOf | `core/blockchain.go:1681-1697` Commit | 等价 | 33 向量 | 事实达成 |
| 回执映射 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:318-321` | `core/state_processor.go:199` MakeReceipt | 等价 | B-2 | 事实达成 |
| 回执扩展字段 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:319` setOpStackMeta | `core/types/receipt.go:596` DeriveFields | 已知分叉 | B-3 | 已确认 |

---

## 阶段 5 矩阵（落库/索引）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 落库入口 | `engine/bcos-engine/EngineServiceImpl.h:1200` registerOpBlock | `core/blockchain.go:1650` writeBlockWithState | 等价 | 单入口落库（OP 块 VALID 后统一走 registerOpBlock） | 已确认 |
| number→hash | `EngineServiceImpl.h:1207-1209` SYS_NUMBER_2_HASH | `core/blockchain.go:1664` rawdb.WriteBlock | 等价 | key=number 十进制串 → value=hash raw 32B；与 op-geth canonical 索引同语义 | 已确认 |
| hash→number | `EngineServiceImpl.h:1213-1216` SYS_HASH_2_NUMBER | `core/blockchain.go:1664` rawdb.WriteBlock | 等价 | key=hash raw 32B → value=number 十进制串 | 已确认 |
| header | `EngineServiceImpl.h:1227-1229` SYS_NUMBER_2_BLOCK_HEADER | `core/blockchain.go:1664` rawdb.WriteBlock | 等价 | OP 头以 tars BlockHeader 落标准表（spec D3：一等公民，同 `Ledger.cpp:234`） | 已确认 |
| 回执 | `EngineServiceImpl.h:1288-1291` SYS_HASH_2_RECEIPT | `core/blockchain.go:1665` rawdb.WriteReceipts | 等价 | key=tx hash（keccak over raw EIP-2718 信封，:1284） | 已确认 |
| 交易 | `EngineServiceImpl.h:1293-1307` SYS_HASH_2_TX（方案 B：`opEnvelopeToTars` 转换后写 tars Transaction，key=keccak 信封；0x04/损坏信封返回 nullopt 跳过写表 D7） | `core/blockchain.go:1664` rawdb.WriteBlock（txs 内联于 block） | 等价 | 方案 B（2026-08-10）：OP 交易落通用 SYS_HASH_2_TX，读侧 eth_getTransactionByHash/eth_getTransactionReceipt 与普通交易同通道可查；s_eth_hash_2_rawtx 写已删（:1253-1254）。残留：0x04 (EIP-7702) 读侧 null（TransactionType 无 handler） | 已确认 |
| 差异点：OP 交易落库（方案 B） | `EngineServiceImpl.h:1293-1307`（opEnvelopeToTars 转换后写 SYS_HASH_2_TX；D7：0x04/损坏信封 nullopt 跳过写表——读侧 null，块仍 VALID）+ `:1253-1254`（s_eth_hash_2_rawtx 写删） | `core/blockchain.go:1664` rawdb.WriteBlock（txs 内联于 block） | 等价 | 2026-08-10 方案 B 后 OP 交易经通用通道可查；0x04 (EIP-7702) 读侧 null 是已知 divergence（TransactionType 无 handler，与 op-geth 不一致） | 已确认 |

> **注（阶段 5，UB）**：`bcos-ledger/bcos-ledger/LedgerMethods.h:237` 对缺失 SYS_HASH_2_TX 行无 `has_value()` 检查直接解引用（循环 :235，`auto field = txEntry->get()`）——pre-existing 缺陷，非 OP 引入（任何块 tx 元数据超 SYS_HASH_2_TX 都会命中）；写入假交易不修复它，只是把可发现的崩溃换成不可发现的错误答案（EngineServiceImpl.h:1247-1252）。

---

## 阶段 6 矩阵（输出）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| block hash | `engine/bcos-engine/EngineServiceImpl.h:799-803`（opHeaderHash 重建比对）+ `:1094-1147`（六项承诺比对） | `core/types/block.go:124` Header.Hash()（RLP keccak） | 等价 | 33 向量 + chainA/B `encodeOpHeader` 字节级全等 | 已确认 |
| 差异点：output root | 无对应（不在 EL 范围） | `optimism/op-node/rollup/output_root.go:29` ComputeL2OutputRootV0（op-node 职责） | 不在范围 | output root 由 op-node（CL 侧）计算，非 EL 执行器对比范围；FISCO gate 测试用 `block.Hash()` 对齐 | 标注 |

---

## 结构性差异（架构层，§2 结构性差异节）

| # | 结构性差异 | FISCO 锚点 | op-geth 锚点 | 说明 |
|---|---|---|---|---|
| 1 | 块校验位置 | 执行后六项承诺比对 `engine/bcos-engine/EngineServiceImpl.h:1094-1147` + blockHash 重建比对 `:799-803` | `core/block_validator.go:51` ValidateBody（预校验） | FISCO 主体仍是执行后承诺比对，无完整 VerifyHeader/ValidateBody 等价物（§0.0 C-13）；→ 阶段2「块校验主体」行 |
| 2 | 双执行器并存 | `opstack-executor/OpstackExecutor.h:54`（v2 独立模块，未装配）+ `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:1014` executeOpBlock（生产单路径） | 唯一路径：`core/state_processor.go:62` Process | v2 模块未装配，生产仅 executeOpBlock 单路径；op-geth 只一条路径（W4-A/D 修正：以 OpstackExecutor.h 为 v2 参照） |
| 3 | 索引隔离（已消除） | 方案 B（2026-08-10）：`EngineServiceImpl.h:1293-1307` 写 SYS_HASH_2_TX（opEnvelopeToTars 转换）+ `:1253-1254` rawtx 写删 | rawdb 统一索引（`core/blockchain.go:1664-1665`） | 已消除：OP 交易落通用 SYS_HASH_2_TX，读侧统一可查；残留 divergence = 0x04 (EIP-7702) 读侧 null（TransactionType 无 handler，块仍 VALID）→ 阶段5「差异点」行 |
| 4 | PBFT 双执行防护 | `OpSchedulerImpl.h:987/:993` executeBlock throw 哑桩（实测定义 :985、throw :991-992） | —（无对应） | 仅在 OP scheduler 装配后生效；OP 模式禁用 PBFT executeBlock（§4 缺口C） |

---

## D 项（已确认，§2 直接纳入）

| # | 差异 | 判定 | 锚点 / 证据 | 状态 |
|---|---|---|---|---|
| D-1 | 交易级执行（费用/L1+operator/Flz/deposit/7702/intrinsic/空账户/CREATE 地址/receipt 共识编码）逐位等价 | 等价 | 记忆审计（v1.101702.2）；→ 阶段3 各交易级行 | 已确认 |
| D-2 | **Karst 占位**：karstConfig 仅 jovianConfig 别名 | 已知分叉 | `bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp:93-101`（karstConfig；:96-98 复制 jovianConfig 仅改 fork tag :97） | 🔴 上线 Karst 前必须按真实 diff 适配 |
| D-3 | Jovian DA footprint 是纯块级 header 字段，不进 tx 级状态 | 等价 | `OpBlockSeal.cpp:196` seal.blobGasUsed=Σ meta.da_footprint（块级）；tx 级 gas_used/cumulative/receiptsRoot/stateRoot 不含 DA（记忆已核实） | 已确认 |
| D-4 | validate↔transition 费用快照契约：FISCO 两阶段共享快照 vs op-geth 即时读 | 已知分叉（契约已固化） | `OpTransition.cpp:328/:237`（共享快照）vs `core/state_transition.go:282/:515`（即时读） | W5 契约测试已固化：opValidate 写 F → slot1 写 F' → opTransition 用 props=F 非 F'（TransitionUsesValidateSnapshot，OpL1EdgeGateTest）；→ 阶段3「差异点#1」行，已确认 |

---

## 差异点归位对照表（§4.2 归位规则，覆盖全部 7 阶段）

| 阶段 | 差异点（comparison doc §1） | 归位 | 指向 | 判定 / 状态 |
|---|---|---|---|---|
| 阶段0 | 三次类型翻译（FISCO 双端三次 vs op-geth 一次） | 矩阵行 | 阶段0「差异点：三次类型翻译」行 | 结构性差异 / 已确认 |
| 阶段1 | 校验位置（validateOpNewPayloadRequest vs checkOptimismPayload） | 矩阵行 | 阶段1「差异点：校验位置」行 | 结构性差异 / 已确认 |
| 阶段2 | 块校验完整度（op-geth VerifyHeader/ValidateBody vs FISCO 承诺比对） | 矩阵行 | 阶段2「块校验主体」行（承诺比对 :1094-1147） | 结构性差异 / 已确认 |
| 阶段3 #1 | 快照契约（两阶段共享快照 vs 即时读） | D-4 | 阶段3「差异点#1 快照契约」行 = D-4 | 已知分叉（契约已固化） / 已确认 |
| 阶段3 #2 | fee 惰性加载（loadOpFeeParams vs L1Block 槽即时解析） | 矩阵行 | 阶段3「fee 惰性加载」行 | 等价 / 已确认 |
| 阶段3 #3 | DA footprint 不进 tx 级 | D-3 | D 项表 D-3 | 等价 / 已确认 |
| 阶段4 #1 | withdrawalsRoot 语义一致（opStorageRoot vs GetStorageRoot） | B-1 | B 台账 B-1 | 等价 / 已修一致 |
| 阶段4 #2 | receiptsRoot 编码（encodeReceiptForRoot vs receiptRLP/depositReceiptRLP） | B-2 | B 台账 B-2 | 等价 / 事实达成 |
| 阶段4 #3 | OP 回执扩展字段（setOpStackMeta vs deriveOPStackFields） | B-3 | B 台账 B-3 | 已知分叉（2 delta）/ 已确认 |
| 阶段5 | OP 交易落库（方案 B：opEnvelopeToTars 转换后写 SYS_HASH_2_TX；0x04 跳过） | 等价 | 阶段5「差异点：OP 交易落库」行 + 结构性差异#3（已消除） | 等价 / 已确认 |
| 阶段6 | output root 不在 EL 范围（op-node 职责） | 不在范围 | 阶段6「差异点：output root」行（标注） | 不在范围 / 标注 |

---

## B 项台账（§4.3，B-5 拆 a/b/c）

| B 项 | 判定 | 状态 | 锚点（FISCO / op-geth） | 依据 |
|---|---|---|---|---|
| B-1 | 等价 | 已修一致 | `bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:170-174` / `consensus/beacon/consensus.go:416-427` | W6 分歧1（opStorageRoot leaf 二次 RLP，OpBlockSeal.cpp:67-97 修复）；`message_passer_write` isthmus+jovian 两向量逐位验证 |
| B-2 | 等价 | 事实达成 | `OpBlockSeal.cpp:138` sealOpBlock / `core/state_processor.go:199` MakeReceipt（+ `core/types/receipt.go:128-148`） | 承诺比对（EngineServiceImpl.h:1094-1147）+ 七项断言 33 向量逐位匹配；正式迁移留 W7 |
| B-3 | 已知分叉（2 delta） | 已确认（动态 manifest 待 W5/RPC 层对拍） | `OpTransition.cpp:319` setOpStackMeta / `core/types/receipt.go:596` DeriveFields + `core/types/receipt_opstack.go:11` | 2 delta：`operator_fee`=FISCO 扩展（`OpTransition.h:96-97` 明标）；legacy `FeeScalar`（pre-Ecotone）FISCO 无；W6 harness 不覆盖回执扩展字段（encodeReceiptForRoot 只共识编码） |
| B-4 | 等价 | 事实达成 | `OpBlockExecute.cpp:143/:198` blockGasLeft 递减 / `core/state_transition.go:282` buyGas | 承诺比对 gasUsed + 七项断言 33 向量（deposit/normal 均递减+回填） |
| B-5a | 等价 | 事实达成 | `OpBlockSeal.cpp:187-197` / `consensus/beacon/consensus.go:429-437` | `jovian_da_mix`（DA=593600=0x90ec0）字节级对拍通过（seal.blobGasUsed=Σ meta.da_footprint :193-194） |
| B-5b | 结构性差异 | 已确认 | `EngineServiceImpl.cpp:442-444` / `core/block_validator.go:119-134`（:131-132 拒绝） | W5 拒绝向量已验证：`jovian_da_mix` 改 `blobGasUsed=gasLimit+1` → INVALID + "DA footprint"（OpL1EdgeGateTest/DAFootprintExceedsGasLimitRejected） |
| B-5c | 等价 | 已确认 | `EngineServiceImpl.cpp:233-240` calcOpBaseFee Jovian max / `consensus/misc/eip1559/eip1559.go:99-107` | W5 jovian 链式对已验证：jovianChainA block1 DA=1,783,600（0x1b3730）> gasUsed=264,840（0x40a88），block2 baseFee=0x3a7e98a8=calcOpBaseFee(DA)（JovianChainedAB） |
| B-6 | 等价 | 已确认 | `OpBlockExecute.cpp:176-178`（calldata[176:178] 提取，`OpBlockExecute.h:66` JovianL1AttributesLen=178）/ `core/types/rollup_cost.go:547-557` ExtractDAFootprintGasScalar | 仅从首笔 L1 attributes calldata 提取，不读任何槽（slot8=OperatorFeeParamsSlot，非 DA scalar）；FISCO 同法 |
| B-7 | 等价 | 已确认 | `OpBlockExecute.cpp:112-116`（首笔 L1 attributes；pre-block system call :106-108 先于首笔）/ `core/state_processor.go:90-95`（beaconRoot/parentHash 预执行） | W5 order-observable 向量已验证：`isthmus_system_call_order_observable`，顺序错 → L1 读者 REVERT → stateRoot 失配 → VALID+七项断言变红（SystemCallOrderObservable）；效果已由 stateRoot 逐位 golden 一致证实（`system_contracts_real`） |
| B-8 | 等价 | 已修一致 | `OpEngineSeam.h:171` computeOpTxRoot / `core/types/block.go:271` DeriveSha（TxHash） | W6 链式对（chainA/B state 延续+跨块 fee）+ 分歧2（txRoot ≥128 笔排序，`bcos-ledger/bcos-ledger/mpt/HashBuilder.cpp:199-231`，排序 :229-230） |

---

## M-B 块级台账（W5，L1 差分 gate）

> W5（L1 差分 gate）对 B 台账 B-5b/B-5c/B-7/D-4 的动态定论。schema 依 spec §6；实测值来自 W5 T2-T4 交付的用例/golden（OpL1EdgeGateTest / OpNewPayloadRpcE2eTest）。

| M-B项 | 验证手段 | 断言 | 用例/golden | 结果状态 |
|---|---|---|---|---|
| M-B5b | 拒绝向量：`jovian_da_mix` 改 `blobGasUsed=gasLimit+1` | INVALID + "DA footprint" | OpL1EdgeGateTest | ✅ 已验证 |
| M-B5c | jovian 链式对：jovianChainA block1 DA=1,783,600 > gasUsed=264,840 | block2 baseFee=0x3a7e98a8=calcOpBaseFee(DA)，VALID（step 3a-2） | OpNewPayloadRpcE2eSuite/JovianChainedAB | ✅ 已验证 |
| M-B7 | order-observable 向量：`isthmus_system_call_order_observable`，顺序错 reader REVERT | VALID + 七项；stateRoot 失配捕获 | OpNewPayloadRpcE2eSuite/SystemCallOrderObservable | ✅ 已验证 |
| M-D4 | opValidate/opTransition 函数对：opValidate 写 F → slot1 写 F' → opTransition 用 props | transition 用快照 F 非 F' | OpL1EdgeGateTest | ✅ 已固化 |

> **注（Task 5 闭合，2026-08-10）**：Ecotone 下 `_op_l1_gas_used` 差分分歧已闭合——`deriveOpReceiptMeta`
> 现读 validate 期快照 `props.ecotone_calldata_gas_used`（= envelope `bedrockCalldataGasUsed`），Fjord+ 仍走
> flz_len 的 Fjord 公式（字段保持 nullopt）。41 向量 t8n 差分门全绿（0 DIVERGE），本文件无需新增
> ALLOWLIST 条目；`ecotone_transfer_basic` / `ecotone_contract_create` 曾报 want=0x130c/0x6b8 got=0x640 已一致。

---

## FINDING-create-output

**回执 output（CREATE 交易）基座层语义分叉（2026-08-11，output 维度新增比对后暴露）**。

门跑新增 `output` 字段比对后，6 个 `*_contract_create` 向量的创建交易回执 `output` 分歧：
- **want** = `0x600160005500`：op-geth `ExecutionResult.ReturnData` 对合约创建 = 部署出的合约字节码（init code 的 return）。
- **got** = `0x`：FISCO `opTransition` 经 vendored evmone `Host::create()` 执行，成功创建后返回
  `evmc::Result{status, gas_left, gas_refund, create_address}`（`bcos-evm/bcos-evm/eth/state/host.cpp:305`
  的 4 参构造，不带 output_data）；创建代码写入 state 账户 `new_acc->code`，而非 result.output_data。

**归属**：**基座层（vendored evmone）语义差异**，非 opstack 层 bug——evmone 的 create 结果不携带
代码到 output，op-geth 的 ReturnData 则携带。回执 `output` 是 FISCO 自有 TARS 扩展字段（非共识编码，
`encodeReceiptForRoot` 不含它，receiptsRoot 不受影响），op-geth 回执本无此字段，故这是
「FISCO 回执 output 与 op-geth 执行结果 ReturnData 的表示差异」而非块执行语义分叉。

**处置**：`attribution=a`（基座层 PENDING-FIX）ALLOWLIST 豁免，**不修**（按线 B 纪律：base 层立案不修；
改动 vendored evmone create 结果携带代码属独立决策，留作后续）。wrapper/precompile 的 output 语义
（截断 quirk、32-byte-1、ecrecover 地址）已全绿，正是本维度新增比对的目的。

<!-- ALLOWLIST vectorId=ecotone_contract_create field=receipts[1].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->
<!-- ALLOWLIST vectorId=fjord_contract_create field=receipts[1].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->
<!-- ALLOWLIST vectorId=isthmus_contract_create field=receipts[1].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->
<!-- ALLOWLIST vectorId=isthmus_contract_create field=receipts[2].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->
<!-- ALLOWLIST vectorId=jovian_contract_create field=receipts[1].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->
<!-- ALLOWLIST vectorId=jovian_contract_create field=receipts[2].output entry=FINDING-create-output attribution=a status=PENDING-FIX want=0x600160005500 got=0x -->


---

## 结构性差异：同父双子分叉（Task 6，chain_fork 载体）

**FISCO -32603 vs op-geth VALID**（已确认，结构性差异）。同一父块下、同高度两个不同 blockHash 的子块：

- **op-geth**：`InsertChain` 接受侧链兄弟块（side chain）→ 新块 VALID，父块保持权威链。
- **FISCO**：`newPayload` 对同一父块（`SYS_NUMBER_2_HASH` 已占用该高度）再次投同高度不同 hash 的块
  → `OpExecutionInternalError`（-32603），latestValidHash=null。

**载体**：`invalid_isthmus_chain_3_fork.json` / `invalid_jovian_chain_3_fork.json`（`_op_canonical` =
canonical 子块 payload，`_op_payload` = sibling）。E2E 两投 runner（OpNewPayloadRpcE2eTest.cpp
runInvalidVector -32603 分支）先投 canonical（VALID 写 SYS_NUMBER_2_HASH 占用）再投 sibling →
抛 OpExecutionInternalError。

**处置**：FISCO 侧保留该行为（PBFT 单一权威链语义），记为结构性差异，不修。
