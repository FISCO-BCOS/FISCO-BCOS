---
status: accepted
date: 2026-11-06
---

# 每条执行 lane 一个 gas 计量策略：只管数量，编译期绑定，lane 内差异走计量规则行

Issue #5772 给 Fisco lane 加一条 intrinsic gas 规则，PR #5773 为此改了 9 个文件：v0 的 `bcos-executor/src/executive/TransactionExecutive.cpp:307` 与 `BlockContext.cpp:147`，v1 的 `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h:170` 与 `vm/HostContext.h:543,823,850`，调度器的 `bcos-scheduler/src/DmcExecutor.cpp:419`。gas 规则散在四个执行器和一个调度器里，在计量点现场读 feature flag。决定：新建模块 `bcos-gas-policy/`，每条执行 lane 恰好一个 gas 计量策略（Fisco、Eth、Op），执行流程只在 13 个计量点向它要 gas 数量。行号以 `7c85d60bf`（`release-3.18.0` 加 #5773）为准。

## 三个事后最容易被问的决定

**只管 gas 数量，不管钱。** 策略回答 intrinsic、执行预算、code deposit、预编译 gas、SSTORE 状态分类、冷热访问、出错时剩余 gas、最终 gasUsed、块内累计。单价、预扣、退款入账、coinbase 小费、fee vault、OP 的 L1 数据费与 operator fee、Fisco 的三套扣费（`BillingTransactionExecutive`、`consumeBalance`、`buyGas`/`refundGas`）都留在原处。Eth 与 Op 现有 policy 里的 `withholdUpfront`、`settleFees`（`ethereum-executor/EthExecutionPolicy.h:275-312`）因此不抽。被否定的备选：把收钱分钱也收进策略，让策略返回一份收费单。否定理由是需求方只要 gas 数量；钱的规则在三个执行器里依赖各自的状态访问方式，收进来会让策略带上动账的顺序与回滚语义。

**每条 lane 一个策略，Fisco 的 v0 与 v1 共用，lane 内差异走计量规则行。** v0（`bcos-executor`）与 v1（`transaction-executor`）的 gas 行为并不相同：21000 的触发条件、code deposit 有无预检、revision 下限都不一样。这些差异不表达为两个策略，而表达为 `FiscoGasRules` 里的字段取值；它每块由执行器版本、feature flag、块版本、系统参数解析一次，计量点不再读 `Features`。Op 的规则行取自现有的 `OpForkSpec`（`opstack-executor/OpForkSpec.h:124`），Eth 的规则行只装 `evmc_revision` 等块环境、策略内部继续按 revision 比较。被否定的备选：v0、v1 各一个策略；一种计量方式一个策略并在块边界用 `std::variant` 分派。否定理由是一条规则（如 #5772）要能只改一处，而开关到规则的对应关系集中在一个纯函数里才能对着开关组合逐格测试。

**编译期绑定，三种接法并存。** Eth 与 Op 共用 `ethereum-executor/EthereumTransition.h:266` 的模板，策略是模板参数，两者之间用 CRTP 复用（`EthGasPolicyBase<Derived>`），与 ADR 0011「Eth 是基类、OP 派生」一致；Fisco 策略不进这棵继承树，独立满足同一个 concept。v1 的 `HostContext`（`transaction-executor/bcos-transaction-executor/vm/HostContext.h:166`）加一个带默认值的模板参数。v0 用类型别名 `using GasPolicy = FiscoGasPolicy`，不模板化：`TransactionExecutive` 被 `bcos-executor/src` 下 72 个文件引用（49 个是预编译合约）并有 5 个子类，加模板参数要改这些文件的签名，而 v0 的代码只服务一个策略，别名与模板参数的运行期开销相同。被否定的备选：虚函数多态（EVM 宿主回调在热路径上）；运行期可加载的插件；Op 非虚继承 Eth（基类计量点互调时会调到 Eth 版本而不是 Op 的重写，编译器不报错）；Op 组合转调 Eth（约一半计量点是纯转调样板）。

## 不在此列的地方

下一块 base fee、excess blob gas、块 gas limit 推导是区块头规则，留在 `bcos-rlp-protocol` 的 `EthPoSHeaderValidation.h`、`bcos-framework/bcos-framework/engine/OpBaseFee.h` 与 engine 服务里。evmone 的 opcode gas 表不动，策略只通过 revision 间接决定它。`bcos-framework/bcos-framework/protocol/TxGasModel.h` 原地不动，由策略调用。准入的 `bcos-tx-validator/bcos-tx-validator/CheckSet.h` 仍是唯一的分派表，策略的算式在启动时绑成可调用对象注入，做法同 `TxValidator.h:78` 的 `RollupCostFn`。RPC 不接策略；`eth_estimateGas` 与 `eth_call` 的区分留在执行器。

## Consequences

- 性质是纯重构：三条 lane 的状态根、回执、gasUsed 逐字节不变。现状里不像有意为之的行为（如 v0 不读交易自带的 gasLimit，`bcos-scheduler/src/BlockExecutive.cpp:236`）原样保留，另开 bugfix PR 处理。其中属于 v0 与 v1 差异或受开关控制的几条，在 `FiscoGasRules` 里各有一个带名字的取值。
- 准入对 Web3 交易的 intrinsic 检查（`bcos-tx-validator/bcos-tx-validator/TxValidator.cpp:667`）与 Fisco 执行的收法今天就不一致；准入绑定的算式复现准入今天的行为，这个不一致作为已知问题保留。
- v0 的 gas 预算与 gasUsed 在调度器里算（`bcos-scheduler/src/BlockExecutive.cpp:236,1703`），调度器与执行器各设计量点、各自解析规则行，跨进程消息不变。
- 开工前置条件：PR #5773 已合入；ADR 0011 的两边裁判（EEST 走生产管线、kona 多块对拍）已绿。Fisco lane 没有外部裁判，重构前先合入特征测试，并用新旧二进制 2+2 混合组链跑 Air CI 集成用例。
- 模块不能反向依赖 `opstack-executor`，Op 的规则行是从 `OpForkSpec` 取 gas 相关字段的视图；EVM 内置预编译与 FISCO `PrecompiledGas` 的价目函数搬进新模块，执行逻辑留在原处。

## FiscoGasRules 字段

v0 与 v1 的差异、各开关的作用，都写成下表的字段取值。每个块开始时调用一次 `resolveFiscoGasRules(executorVersion, features, blockVersion, sysConfig)`，计量点只读结果。字段名是草案，在实现规则行的 PR 里定稿。行号以 `7c85d60bf` 为准。

| 字段 | 由什么决定 | 今天读开关的位置 |
|---|---|---|
| `revision` | `feature_evm_cancun`、`_prague`、`_osaka` 与块版本；v1 下限为 Cancun | `bcos-executor/src/executive/BlockContext.cpp:123`，`transaction-executor/bcos-transaction-executor/vm/HostContext.h:266` |
| `intrinsicUpfront` | `bugfix_tx_intrinsic_gas` | `bcos-executor/src/executive/TransactionExecutive.cpp:307`，`transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h:170` |
| `accessListInIntrinsic` | v1 为真；v0 为假，调度器消息不带 access list | `TransactionExecutorImpl.h:177` |
| `calldataFloorUpfront` | v1，revision 不低于 Prague，且 `intrinsicUpfront` 为假 | `HostContext.h:543` |
| `topCallGas` | v0：开 `feature_balance` 且带 value；v1：每个顶层 CALL；`intrinsicUpfront` 为真时两者都不收 | `TransactionExecutive.cpp:349`，`HostContext.h:850` |
| `codeDepositPerByte` | `intrinsicUpfront` 为真取 200，否则取 20 | `BlockContext.cpp:147`，`HostContext.h:823` |
| `codeDepositPrecheck` 与 `maxCodeSize` | v0 有预检和大小上限（3.2 起 1 MiB，之前 0x40000）；v1 都没有 | `TransactionExecutive.cpp:1157`、`:1177` |
| `budgetSource` | v0：链级 `tx_gas_limit`，系统合约目标取 3000000000，除非开 `bugfix_precompiled_gascalc`；v1：开 `bugfix_gas_payment_balance_precheck` 时取交易与链级的较小值 | `bcos-scheduler/src/BlockExecutive.cpp:236`，`TransactionExecutorImpl.h:53` |
| `storageStatusMode` | `bugfix_evm_storage_status`；关闭时 v0 与 v1 的旧分支不同 | `bcos-executor/src/vm/EVMHostInterface.cpp:87`，`transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h:76` |
| `eip2929` | `feature_evm_eip2929` 且 revision 不低于 Berlin | `bcos-executor/src/vm/Eip2929Util.h:32` |
| `builtinPrecompileCharge` | v0：块版本是否达到 3.1.0；v1：`bugfix_v1_error_handling` | `bcos-executor/src/vm/HostContext.cpp:315`，`transaction-executor/bcos-transaction-executor/precompiled/PrecompiledImpl.h:65` |
| `errorGasLeft` | v0：`bugfix_evm_exception_gas_used`；v1：`bugfix_v1_error_handling` | `TransactionExecutive.cpp:1584`，`transaction-executor/bcos-transaction-executor/EVMCResult.cpp:169` |
| `gasLimitOpcode` | v0：块版本达到 3.1.0 取 `tx_gas_limit`，否则取 3000000000；v1：`tx_gas_limit` | `bcos-executor/src/vm/HostContext.cpp:89`，`HostContext.h:462` |
| `zeroGasUsedForSystemContract` | v0：`bugfix_precompiled_gasused` 关闭时清零，受 `bugfix_dmc_deploy_gas_used` 修正 | `BlockExecutive.cpp:1707`、`:1716` |

## 已知问题

下面这些现状不像有意为之。重构原样保留，日后各开 bugfix PR。

| # | 现状 | 位置 |
|---|---|---|
| 1 | v0 不读交易自带的 gasLimit，每笔都拿链级 `tx_gas_limit` 当预算 | `bcos-scheduler/src/BlockExecutive.cpp:236` |
| 2 | 同一份链配置下 v0 与 v1 的 revision 不同：v1 下限是 Cancun，v0 没有 | `HostContext.h:266`，`bcos-executor/src/vm/VMInstance.cpp:86` |
| 3 | v1 的 code deposit 没有预检，也没有代码大小上限 | `HostContext.h:823` |
| 4 | `bugfix_evm_storage_status` 关闭时，v0 与 v1 返回的 SSTORE 状态不同 | `EVMHostInterface.cpp:87`，`EVMHostInterface.h:76` |
| 5 | Fisco lane 不应用 evmone 的 gas 退款，gasUsed 等于预算减剩余 | `BlockExecutive.cpp:1703`，`TransactionExecutorImpl.h:311` |
| 6 | `CastPrecompiled` 建了计价器但不扣 gas | `bcos-executor/src/precompiled/CastPrecompiled.cpp:46` |
| 7 | v1 余额不足时，gasUsed 由罚金除以单价反推；属于钱的路径，不搬 | `TransactionExecutorImpl.h:291` |
| 8 | Eth 执行器没有运行中的块 gas 计数，每笔都拿整块 gas limit 当余量 | `ethereum-executor/EthereumExecutor.h:424` |
| 9 | v0 在 3.1.0 之前的块上，内置预编译的剩余 gas 计算有旧错误 | `bcos-executor/src/vm/HostContext.cpp:315` 所在函数 |
| 10 | 准入对 Web3 交易总是检查以太坊 intrinsic；Fisco 执行只在开关打开时才收，且 v0 不计 access list | `bcos-tx-validator/bcos-tx-validator/TxValidator.cpp:667` |
| 11 | v0 调度器：系统合约交易在 `bugfix_precompiled_gasused` 开、`bugfix_precompiled_gascalc` 关时，预算是 3000000000，算 gasUsed 却用链级 `tx_gas_limit` 去减；`tx_gas_limit` 低于 3000000000 时差为负，回执与区块的 gasUsed 按 u256 回绕。默认 `tx_gas_limit` 等于 3000000000，不触发 | `BlockExecutive.cpp:1703`（以 `298461a4a` 为准） |

## 特征测试（PR 1）

Fisco lane 没有外部裁判，重构前先用特征测试钉住今天的收费。每个测试在一组开关配置下跑同一段交易脚本，把每个（配置，交易）的状态码、gasUsed、输出摘要与测试文件里的内联表逐行比对；设 `GAS_CHARACTERIZATION_RECORD=1` 时打印当前值而不比对，用来生成这张表。重构 PR 不得改动这些表；改动表格就是改动行为，要在 review 里按行为变更论证。

- 脚本与配置生成在 `bcos-framework/bcos-framework/testutils/GasCharacterization.h`，v0 与 v1 共用。
- 交易分三组。部署：成功、超出代码大小上限、只有旧价付得起的 code deposit、init code revert 或耗尽 gas。调用：转账、带 value 的转账（v0）、普通调用、嵌套调用、revert、EVM 异常、签了较小 gasLimit 的调用（v1）、带 access list 的 EIP-2930 交易（v1）。计量：SSTORE 各状态（含同一交易内对同一槽位写两次）、EVM 内置预编译 0x01–0x09、FISCO `CryptoPrecompiled` 与 `TableManagerPrecompiled.createKVTable`。
- 配置：全关；每个开关单独打开（连同它的依赖）；`bugfix_tx_intrinsic_gas` 与其余开关两两组合；3.18.0 创世；创世上每个开关翻转一次（创世里有的关掉、没有的打开，EVM 分叉与 `feature_balance` 不在创世里）；v0 另有 3.0、3.1、3.2 块版本。开关组合遵守 `Features::getFeatureDependencies`。
- `GasCharacterization_V1Test`（`test-transaction-executor`）同时记录 `protocol::normalizeReceipts` 求出的块 gas 合计，即 `BaselineScheduler` 写进区块头的值。
- `GasCharacterization_V0Test`（`test-bcos-executor`）按串行（`executeTransactions`）和 DMC（`dmcExecuteTransaction` 加一个按 `DmcExecutor::handleExecutiveMessage`/`V2` 处理消息的驱动）各跑一遍。
- `GasCharacterization_SchedulerTest`（`test-scheduler`）用每帧固定扣 1000 gas 的桩执行器，覆盖 `BlockExecutive` 与 `SerialBlockExecutive` 的预算、回执 gasUsed、区块 gasUsed，以及三个 gas 开关的全部组合。
- 混合组链：`tools/.ci/ci_check_air.sh` 设 `FISCO_BCOS_OLD_BIN` 时，每条链的奇数号节点用旧二进制。新旧节点状态一旦不一致就凑不够法定票数，链停止出块。

尚未覆盖：

- 系统合约（`SYS_CONFIG` 等）的直接调用只在调度器测试里覆盖；两个执行器测试的环境下，`SystemConfigPrecompiled` 读表时解引用空存储（`Table::asyncGetRow`），调用无法执行。
- `TablePrecompiled` 与 `KVTablePrecompiled` 的读写：`createKVTable` 在这两个测试环境里以状态 15 失败，只钉住了它失败时的 gas。
- 开 `feature_evm_prague` 且关 `bugfix_evm_create2_delegatecall_staticcall_codecopy` 的配置不进 v0 的 DMC 表，这组配置的执行结果在不同运行之间不一致，另行修复。
