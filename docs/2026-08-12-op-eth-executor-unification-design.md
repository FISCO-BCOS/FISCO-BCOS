# opstack 与 ethereum 执行流程统一设计（执行层 + 存储适配层）

> 状态：2026-08-12 设计定稿。分支 `worktree-op-alignment`。
> 前置文档：`docs/opstack-alignment-plan.md`（层次 1/2 已实施；层次 3 失败回执明确不实施）、
> `docs/opstack-scheduler-adapter-design.md`（§0 已判定 processOpBlock 保留、BaselineScheduler 承载不了 OP 块语义）。
> 本设计在前置决策不变的前提下，统一**执行层 + 存储适配层**——这是两套流程重复实现最集中的部分。

## 0. 背景与问题

仓库现有两条执行流程：

| | ethereum（executor_version=2） | opstack（executor_version>=3）块路径 |
|---|---|---|
| 执行入口 | concept 三阶段逐笔（`createExecuteContext→prepare→execute→finish`） | `processOpBlock` 整块内联逐笔（`opValidate/opTransition` 直接调用，不经概念方法） |
| 存储读桥 | `StorageStateView`（EVMAccount 读，错误吞成 absent） | `Storage2State`（自研，直接 storage2 读，poison 标志） |
| 写回 | `applyStateDiff`（共享，ethereum-executor/BCOS2Evmone.h:94） | `Storage2State::applyDiff`（自研，poison+rethrow） |
| 转换代码 | `bcosTransactionToEvmone` 一份 | 块路径自 `decodeOneRawTx` 直通 evmone 类型，第二份 |

**三份重复**：读桥、写回、转换代码。opstack 的 per-tx/eth_call 路径已经复用 ethereum-executor 的
`StorageStateView/applyStateDiff/bcosTransactionToEvmone`——只有**块路径**独占了 Storage2State 与内联执行。

前置文档已确认不可动摇的决策：
- `processOpBlock` 保留（op-geth 本来就是串行；BaselineScheduler 承载不了 OP 块语义，`docs/opstack-scheduler-adapter-design.md` §0）。
- 失败回执不实施（会破坏 op-geth 对拍共识，`docs/opstack-alignment-plan.md` 层次 3）。

## 1. 目标（保对齐统一）

1. **执行入口统一**：opstack 块路径逐笔执行改经 `OpstackExecutor` 概念方法
   （`executeTransaction` / `executeDeposit` / `finalizeBlock` / 新增 `executeBlockStart`），
   与 ethereum 块路径、eth_call 同一方法面。
2. **存储适配层统一**：`Storage2State` 并入带**可选 poison 通道**的共享读桥（`SharedReadBridge`），
   块路径开通道（错误→整块 OpStorageError），eth/eth_call 关通道（错误→absent，现状不变）。
3. **转换代码一条路**：envelope→`protocol::Transaction`→evmone 只有一份转换实现，块路径不再直通。
4. **语义零变化**：op-geth 对拍全绿（golden 63/33）、现有测试全绿。

## 2. 非目标（本次不动）

- 块级语义：deposit 前置/顺序、fee 中途加载（`loadOpFeeParams`）、gas 池递减、原子拒块、discard-writes —— **全部保留在块编排器**。
- seal/stateRoot/txRoot 计算、六元组比对、错误分类（INVALID vs -32603）。
- `opstackRegisterBlock` 块表写入、engine 三阶段、eth_call 外壳（OpCallScheduler）。
- ethereum v2 路径（已用 concept，语义不变）。
- 块编排器形态统一（op 顺序整块 vs eth 并行/chunk——差异是 FISCO 优化 vs op-geth 串行，保持）。

## 3. 目标形态

```
当前                                              统一后
─────────                                         ────────
executeOpBlock                                    executeOpBlock (块编排器，块级语义全保留)
  ├ decodeOneRawTx → OpBlockTx(evmone)              ├ decodeRawEnvelopes → blockTxList(envelope+type)
  ├ Storage2State (自研桥, poison)                   ├ SharedReadBridge(storage, &poison)   ← 与 eth 共用
  ├ processOpBlock (内联逐笔)                        ├ orchestrator 逐笔循环:
  │    system_call_block_start                      │   ├ executeBlockStart      (新方法)
  │    loadOpFeeParams / blockGasLeft 递减           │   ├ executeDeposit(dep)    (deposit)
  │    opValidate → opTransition → applyDiff         │   ├ executeTransaction(tx) (normal, 概念三阶段)
  │    finalizeOpBlock                               │   └ finalizeBlock          (块尾)
  ├ poison 检查 (不变)                                ├ poison 检查 (不变)
  ├ sealOpBlock + stateRootOf (不变)                 ├ sealOpBlock + stateRootOf (不变)
  └ txRoot + gasUsed → 六元组 (不变)                  └ txRoot + gasUsed → 六元组 (不变)
```

## 4. 组件划分

| 组件 | 位置 | 职责 | 依赖 |
|---|---|---|---|
| `SharedReadBridge`（新/改造） | `ethereum-executor/StorageStateView.h` | evmone StateView + **可选 poison 通道** + 共享写回；承载 Storage2State 的额外读方法 | bcos-evm-eth, storage2 |
| `OpstackExecutor`（扩展） | `opstack-executor/OpstackExecutor.h` | 逐笔方法面：`executeTransaction`/`executeDeposit`/`finalizeBlock`/新 `executeBlockStart` | ethereum-executor 适配层, bcos-evm-opstack |
| `OpBlockOrchestrator`（新） | `opstack-executor/OpBlockExecute.h/.cpp` | 块级循环：deposit 前置/顺序、fee 中途加载、gas 池递减、原子拒块、discard-writes；逐笔调 executor 方法 | OpstackExecutor, EnvelopeToTarsConverter |
| `OpSchedulerImpl::executeOpBlock`（改） | `opstack-executor/OpSchedulerImpl.h` | decode → 编排 → poison 检查 → seal/stateRoot → 六元组 | orchestrator |
| `OpBlockRegister`（不变） | `opstack-executor/OpBlockRegister.h` | 块表落盘 | — |

## 5. 存储适配层统一（SharedReadBridge）

### 5.1 读侧

`StorageStateView`（ethereum-executor）改造为共享读桥，追加**可选 poison 通道**：

```cpp
// 现状：StorageStateView(storage)，错误吞成 absent（eth 语义）
// 改造：StorageStateView(storage, std::optional<std::string>* error = nullptr)
//   - error == nullptr  → 现状行为（读失败 → absent），eth/eth_call/v2 路径不变
//   - error != nullptr  → 读失败记录首个错误到 *error（Storage2State 的 poison 语义），
//                        并让 Orchestrator/executeOpBlock 在块尾检查 → OpStorageError
```

承载项（Storage2State → 共享桥）：
- **poison 通道**：`get_account/get_account_code/get_storage` 的失败路径从"吞 absent"改为"记录错误"
  （由 error 指针开关）。`poison()` 只记首个错误、不重抛——语义对齐 `Storage2State.h:15-18` 的契约。
- **额外读方法**：Storage2State 的 `fetchAllStorage`（live 账户/存储槽遍历，seal/stateRoot 依赖）与
  `AccountView` 等块路径专用方法必须带上共享桥（同样受 poison 通道门控）。
- **读实现选择（验证门控）**：`StorageStateView` 经 EVMAccount 读，`Storage2State` 直接 storage2 读，
  两实现存在差异（has_storage 探针、code_hash 空值处理等）。**默认以 StorageStateView 的 EVMAccount
  读为共享实现**，块路径切换读桥后必须过 op-geth golden；若某读语义不一致，将该分歧逻辑并入共享桥
  （保留 Storage2State 的语义），以双路径一致性测试 + golden 作为判定闸门。

### 5.2 写侧

`applyStateDiff`（ethereum-executor/BCOS2Evmone.h:94）已是共享写回。Storage2State::applyDiff 的
"写失败 poison + rethrow"契约并入共享写回：给 `applyStateDiff` 追加可选 error 通道（或薄封装
`applyStateDiffWithPoison`），块路径用带通道版本，eth 路径用原版。

### 5.3 删除

`opstack-executor/Storage2State.h` 全量删除（其职责全部由 SharedReadBridge + orchestrator 承接），
删除以"grep 引用为零 + 全量编译通过"为完成判据。

## 6. 块编排器重构（逐笔 concept 化）

### 6.1 结构

`processOpBlock`（OpBlockExecute.cpp:97-203）重构为**编排器循环**，块级语义原样保留，
逐笔执行改为调用 `OpstackExecutor` 概念方法：

```
executeBlockStart                              // 原 system_call_block_start（OpBlockExecute.cpp:101）
[首笔 deposit 强制 / 顺序校验]                   // 原块级校验，严格于 spec
fee = loadOpFeeParams(bridge)                  // 块中途加载，后笔复用（OpBlockExecute.cpp:150）
blockGasLeft 递减                              // 每笔按 gasLimit 扣
每笔 normal:
    tx = opEnvelopeToTars(env, hash)           // 物化 protocol::Transaction（注入转换器）
    receipt = executor.executeTransaction(
        storage, header, tx, 0, ledgerConfig, /*call=*/false,
        fee, blockGasLeft, chainId, &hashes)   // 概念三阶段：m_prepare(opValidate) → m_execute(opTransition) → m_finish(applyStateDiff)
每笔 deposit:
    receipt = executor.executeDeposit(storage, header, dep, chainId, blockGasLeft, ledgerConfig, &hashes)
块尾:
    executor.finalizeBlock(storage, header, ledgerConfig)   // finalizeOpBlock + MessagePasser
```

产出仍是 `OpBlockResult{receipts, txTypes, gasUsed, finalizeDiff}`（receipt 顺序 / typed-prefix / 累积
gas 与原 processOpBlock 逐字一致）。

**ledgerConfig 来源（新增约束）**：当前块路径不消费 ledgerConfig（OpSchedulerImpl.h:206-207 注释），但
executor 概念方法都读 `ledgerConfig.evmcRevision()` 做 fork 一致性检查。逐笔经概念方法后，编排器必须
提供 ledgerConfig——来源二选一：
- (a) 编排器从 storage 读：`ledger::getLedgerConfig(view, header.number(), blockFactory)`（对齐
  OpCallScheduler.h:260-261 的获取方式）；
- (b) `OpSchedulerImpl::executeBlock` 已接收的 ledgerConfig 参数向下穿透到 executeOpBlock。
推荐 (a)（编排器自足，不依赖 executeBlock 调用方是否传对），并在双路径一致性测试中验证其
evmcRevision 与 `configAt(timestamp)` 的 fork 判定一致（二者必须同一来源，防 fork/rev 双源漂移）。

### 6.2 新增方法 `executeBlockStart`

原 `system_call_block_start`（OpBlockExecute.cpp:101-102，经 `sanitizeStateDiff` + applyDiff）从编排器
内部提取为 `OpstackExecutor` 公开方法，使**块首/块尾/逐笔全部落在执行器方法面上**（统一执行接口的完整性）。
签名沿用 executeDeposit 的注入风格：`(storage, header, ledgerConfig, chainId, blockGasLeft, hashes)`。

### 6.3 转换无损要求（本设计最关键的不变量）

块路径改经 `executeTransaction` 后，每笔 normal tx 的转换变为：
`envelope → opEnvelopeToTars → protocol::Transaction → bcosTransactionToEvmone → evmone tx`。

**要求**：对每种 EIP-2718 类型（legacy / 2930 / 1559 / 4844 / 7702 及其 OP 组合），该往返结果必须与
`decodeOneRawTx` 直通产出的 evmone tx **逐字节一致**，否则状态根分叉。由双路径一致性测试逐类型验证
（§9.2）；任一类型不无损 → 该类型落回"共享执行核直通"（方案 B 退路，§11 R1）。

## 7. executeOpBlock 整合与 Parity Switch

`OpSchedulerImpl::executeOpBlock`（OpSchedulerImpl.h:236）：
- step1 `decodeRawEnvelopes`：仍产出 blockTxList（deposit 解码 + normal 类型分派），但 normal 不再直通
  evmone tx（转换交给 executeTransaction，见 §6.3）；deposit 解码保留（executeDeposit 需要 DepositTx）。
- step2 桥替换为 SharedReadBridge（poison 通道开）。
- step3 编排器（§6）。
- step4-6 不变（poison 检查 → OpStorageError；seal/stateRoot/txRoot → 六元组）。

**Parity Switch（双路径一致性测试的载体）**：`constexpr bool c_useConceptOrchestrator = false` 起步。
旧 `processOpBlock` 路径与新编排器路径并存；测试对同一语料双跑、比对状态根与回执。全绿后翻转开关为
`true`，删除旧路径（§10 阶段三）。开关放在 `executeOpBlock` 内，编译期消掉死分支。

## 8. 错误分类映射

块路径错误分类不得漂移。原 processOpBlock 的异常通道 → 新路径的映射：

| 原通道 | 新来源 | 分类 |
|---|---|---|
| processOpBlock 内 `throw std::runtime_error`（validate 失败 / deposit 顺序 / gas 超限 / 空块） | `OpTxValidationFailed` / `OpForkRevisionMismatch` / `EvmcRevisionNotConfigured`（executeTransaction/executeDeposit 抛出） | `OpConsensusError`（→ INVALID）——编排器需把 executor 的抛出翻译回原块级异常通道 |
| Storage2State poison | SharedReadBridge poison | `OpStorageError`（→ -32603），不变 |
| applyDiff 写失败 poison + rethrow | `applyStateDiffWithPoison` | `OpStorageError`，不变 |

编排器对 executor 抛出的翻译是**行为面**，必须与现有错误分类测试（OpSchedulerImpl 相关用例）逐一对齐。

## 9. 测试与验收

### 9.1 验收标准（可证伪）

1. op-geth golden **63/33 全绿**（语义零变化）。
2. 现有测试全绿：opstack-executor（OpstackExecutor 9、OpBlockRegister、OpTwoPhase、OpBlockSeal、
   OpCallScheduler 2）、engine、OpNewPayloadRpcE2eTest、e2e。
3. **双路径一致性测试**：同输入（语料见 §9.2）旧 processOpBlock vs 新编排器 → 相同状态根 + 相同回执 + 相同 gasUsed。
4. `Storage2State` 删除后 grep 引用为零 + 全量编译通过。

### 9.2 双路径一致性测试语料

- op-geth golden 63 用例 / 33 向量（现状语料）。
- 增强语料 125 向量（invalid/chain/legacy，仓库已有）。
- **逐 tx 类型探针**（补 golden 覆盖面缺口）：legacy / 2930 / 1559 / 4844 / 7702 各至少一笔，验证
  §6.3 转换无损；含 deposit + normal 混排块、空块、首笔非 deposit 块（结构错误分类）。

## 10. 实施阶段（三步，每步独立验收）

### 阶段一：存储适配层统一（独立可验收）
- SharedReadBridge（poison 通道 + 承载 Storage2State 额外方法）+ `applyStateDiffWithPoison`。
- 块路径读/写切到共享桥（poison 通道开），Storage2State 保留（供 parity 参照）。
- 闸门：op-geth golden 63/33 全绿 + 现有测试全绿。**读语义分歧在此步暴露并并入共享桥。**

### 阶段二：块编排器 + 逐笔 concept 化（核心改动）
- `executeBlockStart` 新增；编排器循环（§6）；executeOpBlock 切到编排器 + Parity Switch。
- 双路径一致性测试在此步跑（新旧路径对照）。
- 闸门：阶段一闸门 + 双路径一致性测试全绿。

### 阶段三：清理与全量回归
- 翻转 Parity Switch → `true`；删除旧 `processOpBlock` 路径与 `Storage2State`。
- 闸门：全量编译 + 全量测试回归 + golden 再确认。

## 11. 风险与缓解

| 风险 | 说明 | 缓解 |
|---|---|---|
| R1 转换不无损 | envelope→tars→evmone 往返在某种 tx 类型上不逐字节一致 → 状态根分叉 | 双路径一致性测试逐类型抓；该类型**落回共享执行核直通**（方案 B 退路） |
| R2 读语义分歧 | StorageStateView(EVMAccount) 与 Storage2State(直读) 存在读差异 | 阶段一用 golden 当闸门；分歧逻辑并入共享桥（保留 Storage2State 语义） |
| R3 错误分类漂移 | executor 抛出 vs processOpBlock 原异常通道的映射错位 | §8 映射表；现有错误分类测试逐一对齐 |
| R4 golden 覆盖缺口 | 63/33 未覆盖某 tx 类型 → 转换问题漏网 | §9.2 逐类型探针补齐语料 |

## 12. 统一后残留差异（预期设定）

统一后 opstack 与 ethereum 的差异收敛为：

**档位一：协议差异（不可统一，op-geth 对齐的定价）**
- 校验/执行函数：opValidate/opTransition（含 blob 拒绝、L1/operator fee）vs validate_transaction/transition
- fee 进 vault + 无块奖励 vs baseFee→coinbase
- deposit / 系统交易存在与否
- 失败语义：整块拒块 vs 失败回执
- 块级状态：首笔 deposit、fee 中途加载、gas 池递减、MessagePasser finalize

**档位二：架构差异（本次范围外，可作后续统一目标）**
- 块编排器形态：OpSchedulerImpl 顺序整块 vs BaselineScheduler 并行/chunk（保持差异才是对齐）
- 块表写入：opstackRegisterBlock vs ledger::prewriteBlockToBuffer（可抽 `appendBlockMetadata` helper）
- eth_call 外壳：OpCallScheduler vs BaselineScheduler::call（外壳同，可下沉共享 coCallLatest）

## 决策记录

- 2026-08-12：用户选定**统一执行接口**（opstack 块路径逐笔经概念三阶段），风险边界**保对齐**
  （op-geth 对拍全绿、语义零变化）。
- 2026-08-12：统一粒度 = **执行层 + 存储适配层**（不含块编排器形态统一，不含块表写入统一）。
- 2026-08-12：验收 = golden 全绿 + 现有测试全绿 + **双路径一致性测试**（旧 processOpBlock vs 新编排器）。
- 2026-08-12：方案 A（编排器重构、逐笔经 OpstackExecutor 概念方法）为主；方案 B（共享执行核直通）
  作为 R1 转换不无损时特定 tx 类型的退路。
- 2026-08-12：实施分三阶段（存储层 → 编排器 → 清理），每阶段独立验收；Parity Switch 承载双路径对比。
