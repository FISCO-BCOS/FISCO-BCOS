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
| base fee | `engine/bcos-engine/EngineServiceImpl.cpp:191`（def）；Jovian max 分支 :233-240 | `consensus/misc/eip1559/eip1559.go:64/:99-107` | 等价 | 一般路径经 33 向量 encodeOpHeader 字节级佐证；**Jovian max 分支属 B-5c（无 jovian 链式对）** | 待W5 |
| DA footprint ==≤GasLimit | `engine/bcos-engine/EngineServiceImpl.cpp:442-444` | `core/block_validator.go:119-134` | 等价 | 均实现（jovian_da_mix DA=593600 字节级） | 事实达成 |
| 单侧：DA 拒绝路径 | `engine/bcos-engine/EngineServiceImpl.cpp:442-444`（已实现未触发） | `core/block_validator.go:131-132` | 结构性差异 | W6 全 VALID 正向向量，`blobGasUsed>gasLimit` 拒绝未触发（B-5b） | 待W5 |

> ⚠️ base fee 的 FISCO 锚点是 `EngineServiceImpl.cpp:191`（def）——§1 写 :233-239 是 Jovian max 逻辑，两者都要在证据列区分；Jovian max 分支状态与 B-5c 保持一致（待W5）。

---

## 阶段 3 矩阵（状态执行）

| 锚点 | FISCO 锚点 | op-geth 锚点 | 判定 | 证据 | 状态 |
|---|---|---|---|---|---|
| 块执行入口 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:99` processOpBlock | `core/state_processor.go:62` Process | 等价 | 首笔 deposit 强制（:112-116） | 已确认 |
| 首笔 L1 attributes | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:112-116` | `core/state_transition.go:346-361` preCheck | 等价 | D-1 交易级 | 已确认 |
| blockGasLeft 递减 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:143/:198` | `core/state_transition.go:282` buyGas | 等价 | B-4 相关 | 事实达成 |
| fee 惰性加载 | `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:157` loadOpFeeParams | `core/types/rollup_cost.go:151/:215/:353`（NewL1CostFunc/NewOperatorCostFunc/NewTotalRollupCostFunc） | 等价 | D-4 相关（静态等价；语义等价待动态证实，W4-C） | 待W5 |
| validate | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:328` opValidate | `core/state_transition.go:346` preCheck | 等价 | D-1 | 已确认 |
| fee 路由至 vaults | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:295-300` | `core/state_transition.go:711-734` | 等价 | C-5 校正后 | 已确认 |
| deposit 执行 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp:441` runDeposit | `core/state_transition.go:473-511` | 等价 | D-1 | 已确认 |
| 差异点#1 快照契约 | `opValidate/opTransition 共享快照（OpTransition.cpp:328/:237）` | `buyGas/innerExecute 即时读（state_transition.go:282/:515）` | 已知分叉 | D-4 | 待W5 |

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
