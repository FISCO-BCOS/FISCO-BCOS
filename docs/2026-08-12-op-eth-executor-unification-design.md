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
4. **语义零变化**：op-geth 对拍全绿（t8n 125 向量 + e2e 81 用例 + golden/engine 79）、现有测试全绿。

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
- **读实现选择（验证门控）**：`StorageStateView` 经 EVMAccount 读，`Storage2State` 直接 storage2 读。
  经 sub-agent 审查确认的真实分歧（**必须显式处理，不能只靠 golden 兜底**）：
  1. **has_storage 探针**：`StorageStateView::hasStorageImpl` 不过滤 tombstone/零值槽（任何非字段 key
     → true）；`Storage2State::probeHasStorage` 只认 live 且非零的 32 字节 slot。**golden 账本由
     Storage2State 写入（零值槽被删），两实现在 golden 上必然同值——golden 全绿不能证明一致**。
     需定向用例：预置含零值/墓碑 slot 的账户 → CREATE/CREATE2 到该地址，双路径比对（EIP-7610 碰撞
     判定）。
  2. **/sys/ 路由**：`EVMAccount(storage, addr, false)` 把 8 个 `c_systemTxsAddress` 路由到 `/sys/`
     （EVMAccount.h:280-291）；Storage2State 恒走 `/apps/`。payload 触碰 FISCO 预编译地址 → 新旧
     路径读写不同表。
  3. **code_hash 空值**：两侧都归一化到 EMPTY_CODE_HASH（一致，保留）。
  4. **get_account_code legacy 回退**：StorageStateView 有 legacy CODE 字段回退，Storage2State 无
     （OP 链代码均经 CODE_HASH 写入，难触发——保留 Storage2State 语义）。
  **默认以 StorageStateView 的 EVMAccount 读为共享实现**，分歧 1/2/4 的 Storage2State 语义并入共享桥；
  以"双路径一致性测试 + e2e golden（经桥）"为闸门。t8n 腿（TestState，不经桥）对读分歧零判别力。
- **poison 通道同实例**：写回错误通道必须与读桥 poison 是**同一个实例**——否则写失败未设读桥 flag，
  executeOpBlock 的 poisoned()-first 判定落空，写故障被误分类为 INVALID。现状（applyDiff 必先设 flag）
  无此窗口，合并版必须接线。
- **executor 块路径读注入**：`executeTransaction/executeDeposit/finalizeBlock` 当前内部各自构造
  `StorageStateView`（读失败静默 absent，不经块路径 poison 通道）。块路径用这些方法时，读失败会
  静默降级——必须给这些方法增加注入桥/poison 指针参数（或改为接收 SharedReadBridge），否则
  §8 的"读桥 poison → OpStorageError"对逐笔读是空的。

### 5.2 写侧

`applyStateDiff`（ethereum-executor/BCOS2Evmone.h:94）已是共享写回。但 **`applyStateDiffWithPoison`
不是薄封装**——`applyStateDiff` 与 `Storage2State::applyDiff` 写回语义有四处真实分歧，必须按
Storage2State 语义逐项并入共享写回：
1. **/apps/ vs /sys/ 路由**：`applyStateDiff` 用 `EVMAccount(storage, addr, false)` 把
   `c_systemTxsAddress` 路由到 /sys/（BCOS2Evmone.h:103）；Storage2State 用 `FromTableName{}` 强制
   /apps/（Storage2State.h:294-301，防 split-brain）。
2. **零值槽**：applyStateDiff 写行不删（零值行残留，BCOS2Evmone.h:124-131）；Storage2State
   `removeOne` + 重读守卫（Storage2State.h:347-377）。切到 applyStateDiff 后账本累积零值行，反向
   放大 §5.1 has_storage 分歧——须保留删行语义。
3. **EIP-161 空账户 create 守卫**：Storage2State 有（:318-325），applyStateDiff 无。
4. **账户删除 / ghost-delete tripwire / 写透缓存**：Storage2State 的 range-删整表 + SYS_TABLES 标记、
   ghost-delete tripwire（:399-403）、块内写透缓存一致性（:383-389）——必须迁移，否则共享桥（若带
   缓存）读到陈旧值。
另：`seeding=true` 播种模式（SeedPreState.h:104-113 依赖）需保留，删除 Storage2State 前先改写
SeedPreState。写回错误通道 = 与读桥 poison **同实例**（先设 flag 再 rethrow，保持现状时序）。

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

**编排器必须显式携带的块级逻辑（sub-agent 审查补全）**：
- D-1 Jovian DA-footprint scalar 覆盖块（读 `txs[0].data[176:178]`，OpBlockExecute.cpp:158-175）——
  块级共识定价逻辑，漏搬 = DA footprint 定价漂移。
- `isL1AttributesTx` 内容校验（OpBlockExecute.cpp:18-26, :108-109）。
- **显式 blockGasLeft 递减**：每笔按 receipt.gasUsed 递减、超限检查（evmone validate_transaction 内部
  `tx.gas_limit > block_gas_left`，编排器逐笔传运行中值）。无双重检查风险——executor 按值接收、只用于
  本次校验，不维护计数器（OpstackExecutor.h:284 确认）。

**ledgerConfig 来源（用户裁定 2026-08-12：块路径豁免）**：当前块路径不消费 ledgerConfig
（OpSchedulerImpl.h:206-207 注释），但 executor 概念方法读 `ledgerConfig.evmcRevision()` 做 fork 一致
性检查。而 `configAt(timestamp)` 恒返回 rev=EVMC_PRAGUE，与 ledger 持久值（缺省回退 EVMC_OSAKA，
LedgerConfig.h:270）**无代码关联**——若经 executor 检查将产生"缺省回退 OSAKA → 整块
OpForkRevisionMismatch→INVALID"的**新拒绝模式**（旧块路径不存在）。**裁定：块路径方法豁免该交叉检查**：
OP 块执行的 fork 唯一来源 = `configAt(timestamp)`（现状 processOpBlock 即如此，从不消费 ledgerConfig）；
m_execute 本就 `(void)ledgerConfig`（OpstackExecutor.h:307），块路径甚至无需 `getLedgerConfig`。
实现：块路径方法加注入开关（跳过 ledgerConfig-evmcRevision 交叉校验）。eth_call 路径（OpCallScheduler
已带 getLedgerConfig 且测试绿）保留该检查，行为不变。§8 的 EvmcRevisionNotConfigured/OpForkRevisionMismatch
抛点在块路径上因此消除（不再需要编排器翻译它们）。

### 6.2 新增方法 `executeBlockStart`

原 `system_call_block_start`（OpBlockExecute.cpp:101-102，经 `sanitizeStateDiff` + applyDiff）从编排器
内部提取为 `OpstackExecutor` 公开方法，使**块首/块尾/逐笔全部落在执行器方法面上**（统一执行接口的完整性）。
签名沿用 executeDeposit 的注入风格：`(storage, header, ledgerConfig, chainId, blockGasLeft, hashes)`。

### 6.3 转换要求（sub-agent 审查后修订——三个最关键的输入不变量）

块路径改经 `executeTransaction` 后，opValidate 的**三个输入**都必须在两条路径上等价：
`evmone tx` + `blockInfo` + **签名信封（signedTxEnvelope，算 L1/DA fee）**。

1. **【共识·必修】签名信封必须是完整信封**：`m_prepare` 当前用 `transaction.extraTransactionBytes()`
   （= encodeForSign **签名前像**，无 r/s/v，Web3Transaction.cpp:225-227）作 signedTxEnvelope。opValidate
   用 env 对**整段**数 0/非 0 字节（Ecotone，RollupCost.cpp:148-156）或 FastLZ（Fjord+，:205）算
   L1/DA fee——签名前像比完整信封少 ~65 字节，只要 L1 fee 参数非 0，两条路径的 L1/DA fee 不同 →
   状态根分叉。**修法：`executeTransaction` 增加 `env`（完整信封）参数，编排器把 decode 保留的 raw
   envelope 传入**（或 opEnvelopeToTars 把完整信封另存供 m_prepare 读取）。
2. **【共识·必修】拒块裁决语义保留**：tars 往返路径缺失两块直通路径的拒绝——chain-id 一致性校验
   （`tx.chain_id == m_chainId`，OpTxDecode.h:95-96 等，缺失则跨链重放 tx 从 INVALID 变 VALID）与
   low-S/EIP-2 校验（requireLowSSignature，OpRlpDecode.h:96-104，缺失则 malleable tx 被收）。两条必须
   保留在转换层或校验层，否则对 op-geth 的裁决漂移。
3. **【执行等价 vs 逐字节】**：`bcosTransactionToEvmone(opEnvelopeToTars(env))` 与 `decodeOneRawTx(env).tx`
   的逐字节差异（v/r/s 恒 0、chain_id 十进制被 safeFromQuantity 按 hex 解析、auth.chain_id≥2^64 截断为
   2^64-1、auth.signer some(0) vs nullopt）经审查多为**执行中立**（CHAINID 取 host、sender 重算、
   recoverAuthority 重算）。验收口径从"逐字节一致"改为"**执行等价**"：逐字段判定是否被执行消费，
   非消费字段豁免；消费字段必须一致。**显式重定义 4844**：L2 无 blob，直通路径 decode 拒 0x03
   （OpTxDecode.h:287-288）、tars 路径支持——不是"待验证类型"，是**拒绝路径 parity**（新旧路径拒绝层
   不同，须对齐同一 INVALID/OpConsensusError 分类）。

由双路径一致性测试逐类型验证（§9.2）；任一**执行消费**字段不等价 → 该类型落回"共享执行核直通"
（方案 B 退路，§11 R1）。

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

**两个新增装配约束（sub-agent 审查补全）**：
- **hashImpl 注入**：`OpSchedulerImpl` 目前无 `hashImpl` 成员（OpSchedulerImpl.h:416-423 只有
  receiptFactory/chainId/forkTimestamps/vm/blockFactory/storage/envelopeToTars），新路径构造
  `OpstackExecutor` 需要 hashImpl——注入为 scheduler 构造参数（与 receiptFactory 同源）。
- **两条分支同一桥类型**：step4-6（poison 检查 / `visitAccounts` / `stateRootOf` / seal）在两条分支
  之外共用，无法类型擦除（`visitAccounts`/`poisoned`/`firstError` 不在 StateView 接口里）——要求两条
  分支使用**同一个桥类型**。故 Parity Switch 阶段二里"旧路径"实为 processOpBlock-on-SharedReadBridge
  （阶段一先切桥），而非 Storage2State。spec §10 的排序已隐含，此处明示。

## 8. 错误分类映射（sub-agent 审查后修订——分类通道必须由编排器翻译）

块路径错误分类不得漂移。**关键机制**：`executeOpBlock` 的双 catch 依赖 -fno-rtti（OpSchedulerImpl.h:269-307）——
`catch(const std::exception&)` 产 OpStorageError(-32603)、`catch(...)` 产 OpConsensusError(INVALID)。
原 processOpBlock 的 `throw std::runtime_error` 逃过 typed-catch → INVALID。而 executor 抛的
`OpTxValidationFailed`/`OpForkRevisionMismatch`/`EvmcRevisionNotConfigured` 都是 `bcos::Exception`
（`virtual std::exception, virtual boost::exception`，bcos-utilities/Exceptions.h:38-48），在 rtti 编译的
代码里**会被 `catch(const std::exception&)` 接住 → 误分类为 -32603**。因此：

- **编排器必须翻译**：捕获 executor 的具体异常类型（**在 §6.1 豁免后，块路径主要剩余
  `OpTxValidationFailed`**——opValidate 失败；fork/rev 检查已豁免不再抛），**重抛为块级
  std::runtime_error**（落入 catch(...) → INVALID）或显式分类为其他。
- **任何漏译类型静默变 -32603**——必须钉死分类测试。
- **§8 表事实修正**：空块/首笔非 deposit/deposit 顺序是**编排器层检查**（留在编排器抛 runtime_error
  → INVALID），不是 executor 抛点。deposit 与 normal 的 gas 超限走不同通道（runDeposit runtime_error vs
  opValidate error_code → OpTxValidationFailed），编排器统一翻译。
- **现有分类测试未钉死**：`OpSchedulerImplSmokeTest.cpp:159-163` 只断言 std::runtime_error、容忍两种
  分类——重构后须补"空块/首笔非 deposit/normal 无效 tx/gas 超限 → 必须 OpConsensusError(INVALID)"
  的钉死用例。
- RTTI 链接方式是涌现属性（libevmone 链接/异常族变化会移位分类），spec 不把 §8 当稳定契约，测试兜底。

| 原通道 | 新来源 | 分类 | 处置 |
|---|---|---|---|
| processOpBlock 内 throw std::runtime_error | 编排器层检查（空块/顺序/结构） | OpConsensusError(INVALID) | 编排器照抛 |
| executor bcos::Exception（validate/fork/rev 检查） | executeTransaction/executeDeposit/finalizeBlock | OpConsensusError(INVALID) | 编排器捕获→重抛 runtime_error |
| SharedReadBridge poison（读） | 逐笔读经注入桥（§5.1） | OpStorageError(-32603) | 块尾 poisoned() 判定 |
| applyStateDiffWithPoison 写失败 | 写回（§5.2） | OpStorageError(-32603) | 与读桥同实例 poison，先 flag 后 rethrow |

## 9. 测试与验收

### 9.1 验收标准（可证伪）

1. op-geth golden 全绿——**t8n 125 向量 + e2e 81 用例 + golden/engine 79**（现状实际计数，spec
   "63/33" 为过时快照）。**两腿性质不同**：t8n 腿经 `evmone::test::TestState` 不经存储桥（管执行语义），
   e2e 腿经真实读桥（管桥语义）——分别作为闸门，不能合并表述。
2. 现有测试全绿：opstack-executor（OpstackExecutor、OpBlockRegister、OpTwoPhase、OpBlockSeal、
   OpCallScheduler）、engine、OpNewPayloadRpcE2eTest、e2e。**注：OpCallScheduler 现有 2 用例不触桥读
   （eth_call happy path 已延期，OpCallSchedulerTest.cpp:12-14）——需补 eth_call 全路径用例**，否则
   SharedReadBridge 改造对 eth_call 读语义无保护。
3. **双路径一致性测试**（需**新建 runner**，§9.2）：同 pre-state 旧路径 vs 新编排器 → 相同状态根 +
   相同回执 + 相同 gasUsed；**并各自独立对 golden 绝对断言**（stateRoot / encodeOpHeader vs op-geth），
   不只互比（防两路径同错，如 fixture 喂了同一个错误 evmcRevision）。
4. **写侧契约迁移测试**（§5.2）：/apps/ 路由、零值删行、EIP-161、ghost-delete、写透缓存逐项对照；
   迁移 `Storage2StateHelpersTest` + 新增"Storage2State vs SharedReadBridge 同 pre-state 双桥 stateRoot
   对比"（golden 对多数写侧契约不可见）。
5. `Storage2State` 删除后 grep 引用为零 + 全量编译通过（**含 SeedPreState.h:104-113 改写**——其
   `applyDiff(seeding=true)` 是真实实例化依赖，不是注释引用）。

### 9.2 双路径一致性测试语料与 runner

**runner（新建）**：建在 e2e/TwoPhase 的 MLS fixture 上（seedPreState + golden header + realConverter/
opEnvelopeToTars + registerVerifiedBlock + parseNewPayloadRequest），同 pre-state 分别驱动旧
processOpBlock 与新编排器。**不要复用 t8n 的 TestState harness**——新路径依赖 MLS 存储 +
protocol::Transaction 物化 + LedgerConfig.evmcRevision，与 TestState 不兼容。**fixture 必须先补
evmcRevision 播种**（§6.1），否则新路径每条向量抛 EvmcRevisionNotConfigured。

**语料**：
- t8n 125 向量 + e2e 81 用例 + golden/engine 79（仓库已入库，manifest.txt 为绑定集）。
- **逐 tx 类型探针**（§6.3 口径）：
  - legacy / 2930 / 1559 / 7702 valid 各至少一笔 → 双路径执行等价 + 独立 golden 绝对断言。
  - **4844**：valid blob 执行探针结构性不可行（L2 无 blob，OpTxDecode.h:287-288）→ 改为**拒绝路径
    parity**（新旧路径拒绝层不同：decode 拒 vs opValidate 拒，断言同一 INVALID/OpConsensusError 分类）。
  - **结构/裁决探针**：空块、首笔非 deposit 块、deposit 混排块、**跨链 chain-id tx**、**high-s tx**
    （§6.3.2 拒块裁决）、**含零值/墓碑 slot 账户的 CREATE**（EIP-7610，§5.1）、**非零 baseFee +
    deposit 内读 BASEFEE**（§11 R6）。

## 10. 实施阶段（三步，每步独立验收）

### 阶段一：存储适配层统一（最难的一步，非"热身"）
- SharedReadBridge（poison 通道 + 读侧分歧 1/2/4 并入 + 写侧四契约迁移 + seeding 模式）+
  `applyStateDiffWithPoison`（与读桥 poison 同实例）。
- 块路径读/写切到共享桥（poison 通道开），Storage2State 保留（供 parity 参照）。
- **executor 块路径方法注入桥/poison 指针**（读失败进 poison，不静默 absent）。
- 闸门：**e2e golden 全绿 + 写侧契约迁移测试全绿**（t8n 腿对桥语义零判别力，"全量 golden"表述会
  高估此步可验收性）。读语义分歧在此步暴露并并入共享桥。

### 阶段二：块编排器 + 逐笔 concept 化（核心改动）
- `executeBlockStart` 新增；编排器循环（§6，含 D-1 覆盖、blockGasLeft 显式递减）；**编排器翻译
  executor 异常 → 块级 runtime_error**（§8）；executeOpBlock 切到编排器 + Parity Switch +
  **hashImpl 注入 + 同一桥类型**（§7）。
- 双路径一致性测试在此步跑（新建 runner + fixture evmcRevision 播种，§9.2）。
- 闸门：阶段一闸门 + 双路径一致性测试全绿 + 分类钉死用例全绿（§8）。

### 阶段三：清理与全量回归
- 翻转 Parity Switch → `true`；删除旧 `processOpBlock` 路径与 `Storage2State`（含 **SeedPreState 改写
  为共享写回 + seeding 豁免**、注释引用清理）。
- 闸门：全量编译 + 全量测试回归 + golden 再确认。

## 11. 风险与缓解

| 风险 | 说明 | 缓解 |
|---|---|---|
| R1 转换执行不等价 | 往返在**执行消费字段**上不等价 → 状态根分叉 | 双路径一致性测试逐类型抓；该类型**落回共享执行核直通**（方案 B 退路） |
| R2 读语义分歧（has_storage / /sys/） | EVMAccount 与 Storage2State 读差异；golden 账本使 has_storage 分歧不可见 | 分歧并入共享桥；定向用例（零值/墓碑槽 CREATE，§5.1） |
| R3 错误分类漂移 | executor 的 bcos::Exception 绑定 typed-catch → 误分类 -32603 | 编排器捕获→重抛 runtime_error；分类钉死用例（§8） |
| R4 golden 覆盖缺口 | 语料未覆盖某 tx 类型/裁决 → 转换与裁决问题漏网 | §9.2 逐类型 + 结构/裁决探针补齐 |
| R5 **签名信封**（新增·共识） | m_prepare 用签名前像（extraTransactionBytes）而非完整信封算 L1/DA fee → 状态根分叉 | §6.3.1：executeTransaction 加 env 参数，编排器传完整信封；双路径测试含 L1 fee 参数非 0 向量 |
| R6 **deposit base_fee=0**（新增） | executeDeposit 硬编码 base_fee=0，旧路径读 header baseFee；deposit 读 BASEFEE 且 baseFee≠0 时分叉 | 改从 header 读 baseFee；或双路径测试加"非零 baseFee + deposit 读 BASEFEE"探针 |
| R7 **evmcRevision 双源**（已裁定） | getLedgerConfig（SYS_CONFIG，缺省 OSAKA）与 configAt（恒 PRAGUE）无代码关联 → 若经 executor 检查则整块 INVALID | **已裁定：块路径豁免交叉检查**（§6.1），fork 唯一来源 configAt(timestamp)；eth_call 路径保留检查 |
| R8 **写侧契约迁移**（新增） | applyStateDiffWithPoison 非薄封装，四处写语义须迁移，否则账本卫生/路由漂移 | §5.2 逐项迁移 + 写侧契约测试 + 双桥 stateRoot 对比 |

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
- 2026-08-12：**sub-agent 四方审查完成并全量并入 spec**。关键修订：签名信封（R5，§6.3.1 完整信封
  参数）、错误分类通道（R3，§8 编排器翻译 bcos::Exception→runtime_error + 分类钉死）、evmcRevision
  双源升级为硬约束（R7，§6.1 genesis 固化或豁免 + fixture 播种）、has_storage 分歧逃逸 golden
  （R2，§5.1 定向用例）、写侧契约非薄封装（R8，§5.2 五契约迁移）、双路径测试需新建 runner + fixture
  evmcRevision 播种 + 双轨绝对断言（§9）、golden 计数更正 63/33→125/81/79（§9.1）、4844 重定义为拒绝
  parity（§6.3）、hashImpl 注入 + 同一桥类型（§7）、编排器遗漏逻辑补全（D-1 DA 覆盖、blockGasLeft
  显式递减、isL1AttributesTx 校验，§6.1）、executor 块路径读注入 poison（§5.1）。
- 2026-08-12：**用户确认三条裁定并落进 spec**：① 签名信封修法 = `executeTransaction` 增加 `env`
  （完整信封）参数、默认空，块路径传 raw envelope、eth_call 行为不变（§6.3.1）；② evmcRevision 处置 =
  块路径方法**豁免**交叉检查，fork 唯一来源 `configAt(timestamp)`，eth_call 保留检查（§6.1）；③ 验收口径
  = **执行等价**（逐字段证明执行中立，状态根一致为最终验证），chain-id 拒绝 + low-S 拒绝**不豁免**
  （§6.3.3 / §6.3.2）。
