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

## 已知阻塞清单

详见设计 rev.2 §4。要点(随实现推进维护):

- `blockHeaderToBlockInfo` 不复用(见上),`buildOpBlockInfo` 用 timestamp 原样。
- 默认 fork 为 `jovianConfig()`(Karst 尚未适配,为占位别名)。
- `ExecuteContext::prepare/execute/finish` 与 `executeTransaction` 的方法体在 Task 2 实现
  (本骨架仅有声明 + `buildOpBlockInfo` 内联实现)。

## 演进路径

1. Task 1(本模块):骨架 + 类定义。
2. Task 2:`executeTransaction`/`executeDeposit`/`finalizeBlock` 方法体。
3. Task 3-5:真实执行 / L1+operator fee+receipt meta / deposit+finalize 测试。
4. Task 6:全量验证 + 收尾。
