# opstack-executor

OP Stack(Optimism L2)交易执行器——`bcos::executor_v1::opstack::OpstackExecutor`。

## 模块目的

在 bcos-evm/opstack 之上提供完整的 OP 交易执行器,作为 OP 验证者闭环(OP 块执行器)的执行入口。
与 `EthereumExecutor` 的关系:同为 `bcos::executor_v1::TransactionExecutor` 概念的实现,但驱动的是
bcos-evm/opstack 的 OP 流水线(opValidate → opTransition),而非 evmone 原生 validate/transition。

## 与 ethereum-executor 的关系(复用适配器)

- **复用**:`StorageStateView`(存储态视图)、`applyStateDiff`(state-diff 回写)、`evmoneReceiptToBcos`
  (base 回执转换)——均来自 `ethereum-executor`(PR #5366),本模块通过 `target_link_libraries(... ethereum-executor)` 复用。
- **不复用**:`blockHeaderToBlockInfo`(EEST 校准)。OP 头 timestamp 存秒,该校准会 /1000 到 ~1970;
  改用 `buildOpBlockInfo`(timestamp 原样,gasLimit/baseFee 由编排层注入)。

## 方法职责

| 方法 | 职责 |
|------|------|
| `executeTransaction` | 执行单笔 OP 普通交易(注入版 opValidate/opTransition) |
| `executeDeposit` | 执行单笔 OP 0x7E deposit(复用 runDeposit) |
| `finalizeBlock` | OP 块级 finalize(无块奖励,经 finalizeOpBlock) |
| `buildOpBlockInfo` | 构建 OP blockInfo(timestamp 原样 / gasLimit 注入 / baseFee 注入) |

## 注入版语义

本执行器是 header-only 模板(由 storage 类型驱动),采用 **INJECTION 风格** opValidate/opTransition:
编排层(OP 验证者/调度器)注入以下参数,而非执行器自行推导:

- `fee`(OpFeeParams,含 D-1 attributes-calldata DA-scalar 覆盖)
- `blockGasLeft`(递减的块级 gas 余量)
- `chainId`
- `blockHashes`(真实块哈希)

语义对齐 `processOpBlock`。

## Deposit 解码归属

deposit 解码(raw envelope → `DepositTx`)**不是本模块职责**——调用方传入已解码的 `DepositTx`。

## 实现状态(2026-08-06)

已完成(基于 v1 feat-op-validator-loop 移植,适配 v2 OP API):

- `executeTransaction`/`executeDeposit`/`finalizeBlock` 已实现(INJECTION 风格 opValidate/opTransition,
  复用 v2 `runDeposit`/`finalizeOpBlock`)。
- `ExecuteContext<Storage>::prepare/execute/finish` + `createExecuteContext` 满足 `TransactionExecutor` concept。
- v2 适配:`opTransition`/`runDeposit` 直接产 FISCO 回执(OP meta + effectiveGasPrice 已投影),
  无需 v1 的 OpTxReceipt 包装与 meta 转换。
- 测试:`opstack-executor-tests` 9/9 绿(transfer/fork/reject/charge/meta/deposit/finalize)。

已知说明:

- 默认 fork 为 `jovianConfig()`(Karst 尚未适配,为占位别名)。
- 交易构造:测试用字段直接构造 EIP-2930(绕开 v1 raw-RLP fixture 与 v2 decode 的兼容差异)。
