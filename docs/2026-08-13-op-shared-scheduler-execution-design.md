# OP 块执行经共享调度器（方案 B）设计

> 状态：2026-08-13 设计定稿（brainstorming 四节通过）。分支 `feat-op-block-scheduler-standalone`（基址 feat-op-block-scheduler `28338d846`）。
> 前置：本分支已交付"独立 OpScheduler"（A 方案，全绿）——BaselineScheduler.h 恢复 base 零 diff、SchedulerSkeleton 删除、OpScheduler 独立实现 SchedulerInterface。本设计在 A 之上做有界改造，把逐交易执行切到共享 `SchedulerSerialImpl`。

## 1. 背景与问题

### 1.1 一路的演进

1. 最初 OP 复用 ethereum 的 `SchedulerSkeleton`（骨架）→ BaselineScheduler.h 被重构（+169/−570），PR 里 ethereum 侧改动太大。
2. 独立方案（A）：撤销骨架，OpScheduler 独立实现 `SchedulerInterface`，逐交易走 `runOpBlockInjection`。BaselineScheduler.h 零 diff，全部测试绿。**已交付**。
3. 用户要求方案 B：OP 的逐交易执行**真正走共享的 `SchedulerSerialImpl`**（统一调度器本体），加 context 参数携带 OP 状态，串行模式保证线性。

### 1.2 前提（不可违背）

**OP 只用线性执行，绝不使用并行调度器。** 理由（OP 语义的必然）：

- `blockGasLeft` 池跨 tx 递减——每条 tx 的校验用前一条递减后的值；
- state diff 跨 tx 可见——每条 tx 读到前一条写后的状态；
- deposit 严格有序——L1 attributes 注入有顺序。

这三条要求"每条 tx 完整走完生命周期（校验→执行→落 diff→扣 gas）后才轮到下一条"，即线性执行。

## 2. 目标与决策

### 2.1 目标

OP 与 ethereum 统一"**调度器机器**"：同一个 `SchedulerSerialImpl` 类型、同一个 `TransactionScheduler` / `executor_v1::TransactionExecutor` concept、同一个 `createExecuteContext → prepare → execute → finish` 生命周期契约。执行语义差异（deposit、六项承诺、串行、Storage2State、announced hash）**保留**——它们是 OP 块语义的本质。

### 2.2 关键决策

| 决策 | 定案 | 理由 |
|---|---|---|
| 调度器 | OP 走 `SchedulerSerialImpl` **串行模式**（max_tokens=1 + chunk=1） | pipeline 跨 stage 并发会破坏 OP 的跨 tx 依赖（见 §8.1）；串行模式 = 顺序 for 循环 |
| 共享接口参数 | **per-executor `BlockContext`**（`TransactionExecutor::BlockContext`），不写死 OP 类型；默认 `= {}` | 接口"出入各种类型"，转换在执行器内部；默认值使 5 参调用有效 → BaselineScheduler.h 零改动 |
| deposit | **方案 A**：deposit 执行逻辑**搬进 ExecuteContext 生命周期**（prepare 构建 DepositTx → execute 走 `runDeposit` → finish apply diff）；transition 复用 `runDeposit`（`executeDeposit` 的共享核心），**不手写** deposit transition | 与"统一生命周期"目标一致；`runDeposit` 保留 mint/is_system_tx/L1 attributes 注入语义 |
| runOpBlockInjection | **删除**（逐笔循环被 SchedulerSerialImpl 取代），拆成**三段**：块前步骤（system_call + deposit-first/Jovian 校验 + hashes 构造）+ 块后收尾 `finalizeOpBlockResult` | OpScheduler 成为唯一执行入口；dual-path 测试改单路径 |
| 错误面 | 保留在 OpScheduler 的 typed-catch（OpConsensusError/OpStorageError 保真 + RTTI-bypass 归一）；**ExecuteContext 内把 `OpTxValidationFailed` 归一为 `OpConsensusError`** | 与现状一致（见 §6 M1） |

## 3. 架构

```
                    OpScheduler（SchedulerInterface，编排不变）
                    executeBlock → coExecuteBlock（m_pending / commitPersist / verify / fastPath / 连续性）
                                          │  execute()（唯一改动）
                                          ▼
                    ① 块前步骤（runOpBlockInjection 头部，抽成 preBlockOpSteps）
                       system_call_block_start + applyStateDiff
                       deposit-first 检查 + Jovian shape 校验
                       构造 RecentBlockHashes（hashes）
                                          │
                                          ▼
            BlockContext = OpBlockExecutionContext{                    ← per-executor 类型
                fee(可变, 惰性加载), blockGasLeft(可变), blockHashes*, chainId}
                                          │
                                          ▼
            SchedulerSerialImpl（共享调度器，串行模式：max_tokens=1 + chunk=1）
            逐 tx：create → prepare → execute → finish（= 顺序 for 循环）
                                          │
                                          ▼
            OpstackExecutor::ExecuteContext（改造）
            prepare：isDepositTx() 分流（depositFromTransaction 自建）→ 普通 tx 读 context（fee 惰性/gasLeft）
            execute：deposit → runDeposit；普通 tx → opTransition（读 hashes/chainId）
            finish：applyStateDiff + setCumulativeGasUsed + blockGasLeft -= gasUsed
                                          │
                                          ▼
                    ② 块后收尾 finalizeOpBlockResult（从 runOpBlockInjection 抽出）
                       finalizeBlock + MessagePasser + sealOpBlock + stateRootOf + computeOpTxRoot
                       + 顺序回填 cumulativeGasUsed + hashErr 检查
```

## 4. 接口设计

### 4.1 per-executor BlockContext（共享接口不写死类型）

```cpp
// bcos-framework 或 transaction-scheduler 头（namespace bcos::scheduler_v1）
struct EmptyBlockContext {};

// SFINAE：执行器定义了自己的 BlockContext 就用它，否则回退 EmptyBlockContext
// （不要求所有 TransactionExecutor 声明 BlockContext —— EthereumExecutor / 测试 executor 零改动）
template <class E, class = void>
struct BlockContextOf { using type = EmptyBlockContext; };
template <class E>
struct BlockContextOf<E, std::void_t<typename E::BlockContext>> { using type = typename E::BlockContext; };

// TransactionScheduler concept 保持 5 参检查（requires 块不改）：
//   scheduler.executeBlock(storage, executor, blockHeader, transactions, ledgerConfig)
// executeBlock 加默认 6th 参（见 §4.2），5 参调用仍有效 → BaselineScheduler.h 零改动

// 各执行器声明自己的类型：
struct OpstackExecutor {
    using BlockContext = OpBlockExecutionContext;   // {fee(可变), blockGasLeft(可变), blockHashes*, chainId}
    // ExecuteContext 内部自己转换：OpBlockExecutionContext → props / transition 输入
};
struct EthereumExecutor {
    // 不声明 BlockContext —— BlockContextOf<EthereumExecutor> 回退 EmptyBlockContext（零改动）
};
```

- 参数类型是 `BlockContextOf<TransactionExecutor>::type`——**每个执行器一个类型**（"出入各种类型"）；
- 转换在**执行器内部**（ExecuteContext 读自己的 BlockContext → props / transition 输入）；
- 默认 `= {}` → 5 参调用（BaselineScheduler.h）仍有效，零改动；
- 各 `BlockContext` 需**默认可构造**；
- **可变性**：`fee` 与 `blockGasLeft` 跨 tx 可变（fee 惰性加载、gasLeft 递减），因此 BlockContext 参数**不用 const**（或用 `mutable` 字段），否则 finish() 内 `blockGasLeft -= ...` 无法编译（见 §4.3）。

### 4.2 SchedulerSerialImpl 串行模式

```cpp
class SchedulerSerialImpl {
    explicit SchedulerSerialImpl(IOServicePool::Ptr pool,
        std::size_t chunkSize = 0, bool serial = false);
    // chunkSize=0（默认）→ 现公式 max(count/max_concurrency, MIN_TRANSACTION_GRAIN_SIZE)
    // serial=true → executeBlock 内强制 chunk=1 + max_tokens=1 → 退化为逐 tx 顺序循环
};

// executeBlock 加默认 6th 参（concept 5 参调用仍有效）：
template <class Storage, executor_v1::TransactionExecutor<Storage> TransactionExecutor>
task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage& storage,
    TransactionExecutor& executor, protocol::BlockHeader const& blockHeader,
    ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig,
    typename BlockContextOf<TransactionExecutor>::type const& ctx = {});
```

- ethereum 装配传默认（零漂移）；OP 传 `serial=true`。
- 必须**同时** `max_tokens=1` 和 `chunk=1`——单改 chunk 不够（见 §8.1）。
- **serial 模式自身兑现 chunk=1**：`executeBlock` 内 `if (m_serial) chunk = 1;`，不依赖调用方传 `chunkSize=1`（防误用）。

### 4.3 OpstackExecutor::ExecuteContext 改造

```cpp
ExecuteContext<Storage>::prepare() {
    if (transaction.isDepositTx()) {
        auto dep = OpstackExecutor::depositFromTransaction(transaction);  // 自建，无需 deposits 向量
        m_deposit = dep;                                                    // execute 阶段消费
    } else {
        // fee 惰性加载（H1）：第一个普通 tx 处才读，此时 deposit 已执行并写入 L1Block 槽
        if (!m_ctx->feeLoaded) { m_ctx->fee = loadOpFeeParams(...); m_ctx->feeLoaded = true; }
        // OpTxValidationFailed → OpConsensusError 归一（M1）
        try {
            m_props = executor.m_prepare(storage, header, tx, ledgerConfig, m_ctx->fee, m_ctx->blockGasLeft);
        } catch (const OpTxValidationFailed& e) {
            throw OpConsensusError(std::string("op block: ") + e.what());
        }
    }
}
// execute()：
//   deposit → executor.runDeposit(storage, header, m_deposit, m_ctx->chainId, m_ctx->blockGasLeft,
//                                 ledgerConfig, m_ctx->blockHashes)   ← 复用 runDeposit（M3），不手写
//   普通 tx → executor.m_execute(..., m_ctx->chainId, m_ctx->blockGasLeft, m_ctx->blockHashes)  ← H3
// finish()：applyStateDiff + setCumulativeGasUsed(累积) + m_ctx->blockGasLeft -= receipt->gasUsed()  ← H4
```

- **fee 惰性加载（H1）**：fee 不能块首快照——`loadOpFeeParams` 必须在本块 L1 attributes deposit 执行**之后**读（OpFeeParams.h 注释：consensus-critical）。故 `fee` 是 `OpBlockExecutionContext` 的可变字段 + `feeLoaded` 标志，在第一个普通 tx 的 prepare 阶段惰性加载。
- **hashes/chainId（H3）**：`m_ctx` 携带 `blockHashes*` + `chainId`，execute 阶段传给 `m_execute`/`runDeposit`，否则 CHAINID=0、BLOCKHASH=全零。
- **cumulativeGasUsed（H4）**：finish() 内顺序累积并 `setCumulativeGasUsed`——`encodeReceiptForRoot` 依赖它，否则 receiptsRoot 错。

### 4.4 OpScheduler::execute() 数据流

```
① 块前步骤（preBlockOpSteps，从 runOpBlockInjection 头部抽出）：
   system_call_block_start(stateView, blk, hashes, cfg.rev, vm) + applyStateDiff        ← H2
   deposit-first 检查（首 tx 必须 L1 attributes）+ Jovian shape 校验                    ← M2
   构造 RecentBlockHashes hashes(view, number, parentHash, &hashErr)

② 建 context（fee 不在此加载——惰性）：
   OpBlockExecutionContext ctx{ .blockGasLeft = header.gasLimit, .blockHashes = &hashes,
                               .chainId = m_chainId, .feeLoaded = false }

③ 逐笔（SchedulerSerialImpl 串行模式）：
   SchedulerSerialImpl(serial=true).executeBlock(view, executor, header, txs, ledgerConfig, ctx)
   → 返回按序 receipts（cumulativeGasUsed 已在 finish() 内回填）

④ 块后收尾（finalizeOpBlockResult）：
   检查 hashErr（非空 → OpStorageError）                                               ← B2
   finalizeOpBlockResult(view, header, cfg, receipts, rawTxBytes, cumulative)
   → finalizeBlock + MessagePasser + sealOpBlock + stateRootOf + computeOpTxRoot
   → OpExecuteBlockResult（cumulative 从 receipts 求和）
```

## 5. 组件边界

### 删除
- `runOpBlockInjection`（整个函数）——拆成 `preBlockOpSteps`（头部）+ 逐笔循环（被 SchedulerSerialImpl 取代）+ `finalizeOpBlockResult`（尾部）；
- OpScheduler::execute() 里的旧循环；
- OpDualPathEquivalenceTest 的 route B（双路径 → 单路径）。

### 新增
- `OpBlockExecutionContext`（OpstackExecutor::BlockContext）：`{fee(可变), feeLoaded, blockGasLeft(可变), blockHashes*, chainId}`；
- `bcos::scheduler_v1::EmptyBlockContext`（ethereum 默认）+ `BlockContextOf<E>` SFINAE；
- `preBlockOpSteps(view, header, cfg, rawTxBytes, deposits, executor, hashImpl)`——块前系统调用 + deposit-first/Jovian 校验 + 构造 hashes；
- `finalizeOpBlockResult(view, header, cfg, receipts, rawTxBytes, cumulative)`——块后收尾（含 hashErr 检查）；
- SchedulerSerialImpl 串行模式（构造参数 `chunkSize` + `serial`，serial 内强制 chunk=1）；
- ExecuteContext 的 deposit 分流 + context 读取（`depositFromTransaction` 自建 DepositTx）；
- OpScheduler ctor 加第 8 参 `ioServicePool`（**必填**，M4）。

### 保留（不变）
- OpScheduler 编排：coExecuteBlock/coCommitBlock、m_pending、commitPersist、fastPathHit、commitContinuityCheck、六路 verify；
- OpScheduler 纯虚：call/getCode/getABI/getPendingStorageAt（eth_call 走 `executeTransaction`，保留）；
- seam（OpSchedulerSeam）、引擎装配——零改动（**Initializer 例外**：OpScheduler ctor 加 ioServicePool，见 M4/B7）；
- `executeTransaction` 保留（eth_call 注入路径）；`executeDeposit` **保留**（eth_call 的 deposit 分支仍调它）；块级 deposit 的 execute 阶段**复用 `runDeposit`**（`executeDeposit` 与 ExecuteContext 共用，防漂移）。

## 6. 错误处理

- 异常分类**保留在 OpScheduler**：`execute()` 用现有 typed-catch（OpConsensusError/OpStorageError 保真重抛 + `catch(...)` RTTI-bypass 归一），包住 preBlockOpSteps + SchedulerSerialImpl 调用 + finalizeOpBlockResult。错误面不变：OpConsensusRejected / OpStorageFault / UnknownError 映射不变。
- **M1（OpTxValidationFailed 归一化）**：`ExecuteContext::prepare()` 的普通 tx 分支内把 `OpTxValidationFailed` 归一为 `OpConsensusError`（沿用 runOpBlockInjection 的消息前缀 "op block: "），否则坏 tx 从 INVALID（OpConsensusRejected）漂到 UnknownError(-32603)。deposit 的 malformed 字段同理。
- 串行模式下异常沿 `co_await` 链直接传播——已对照 oneTBB 源码确认：`syncWait` 捕获并原样重抛、`parallel_pipeline` 把 filter 异常透传给调用者、`arena.execute` 透传。实现时补一个单元测试（构造抛 OpConsensusError/OpStorageError 的 tx，断言经 SchedulerSerialImpl 后仍分类正确）。

## 7. 测试策略

| 阶段 | 测试 | 目的 |
|---|---|---|
| 过渡期（runOpBlockInjection 尚在） | 新增等价测试：OpScheduler(serial SchedulerSerialImpl) 结果 == 旧 runOpBlockInjection 结果 | 证明 B 行为不变 |
| 过渡期后 | 删 runOpBlockInjection + route B | 唯一路径化 |
| OpDualPathEquivalenceTest | 改单路径：OpScheduler(via SchedulerSerialImpl) vs golden（`_op_expected` stateRoot）+ t8n 语料 | golden 守护保留 |
| test-transaction-scheduler | 新增：默认 pipeline vs serial 对同一交易序列产出相同 receipts | 证明 serial 开关对 ethereum 零漂移 |
| opstack-executor/detail/block | 全量重跑 | 回归 |
| OpstackExecutorTest | 补 deposit 走 ExecuteContext 生命周期用例 | deposit 三阶段覆盖 |

## 8. 可行性核验（关键技术发现）

### 8.1 pipeline 跨 stage 并发 → 串行模式必须 max_tokens=1 + chunk=1

SchedulerSerialImpl 是 4 级 tbb pipeline（create→prepare→execute→finish），filter `serial_in_order` + 3 线程 arena。`serial_in_order` **只保证单个 filter 内顺序**，不阻止**跨 filter 并发**（stage N+1 处理 chunk k 时，stage N 处理 chunk k+1）。

因此即使 **chunk=1**，tx 1 的 prepare（stage 2）会与 tx 0 的 execute/finish（stage 3/4）**并发执行**——对 OP 的跨 tx 依赖是竞态：
- blockGasLeft 池：tx j 的 prepare 读到 tx j-1 还没扣完的 gas；
- state diff：tx j 的 execute 读到 tx j-1 还没落下的 diff。

只有 **max_tokens=1**（整个 pipeline 同时只有一条 tx）才彻底消除跨 stage 并发 → 退化为严格 for 循环 = runOpBlockInjection 行为。**这是 B 正确性的必要条件。**

### 8.2 stateRoot/六项承诺复刻

`finalizeOpBlockResult` 需要 finalizeBlock → MessagePasser 快照 → sealOpBlock → stateRootOf(bridge) → computeOpTxRoot（OpBlockExecute.h:285-310）。抽成共享函数后，runOpBlockInjection（过渡期 route B）与 OpScheduler 调同一个 → 双路径对比天然保真。

### 8.3 deposit 生命周期化 + BlockContext 字段

ExecuteContext 直接 `depositFromTransaction(transaction)` 自建 DepositTx，去掉现 execute() 里的预解码 deposits 向量。BlockContext 字段（修正：不是"缩为 {fee, blockGasLeft}"，而是）`{fee(可变), feeLoaded, blockGasLeft(可变), blockHashes*, chainId}`——审查证明 fee 需惰性加载（H1）、hashes/chainId 需注入（H3），四个字段都不可少。

## 9. 范围（明确不做）

- **不做并行**：OP 绝不走 SchedulerParallelImpl（前提）。串行正确性押在 `max_tokens=1`，OP 装配路径加断言（`assert(m_serial)`）。
- **不改 ethereum 执行语义**：SchedulerSerialImpl 串行模式默认关闭，ethereum 装配零改动；EthereumExecutor 零改动（`BlockContextOf` SFINAE 默认 EmptyBlockContext）。
- **不引入骨架**：SchedulerSkeleton 不复活；OpScheduler 保持独立编排。
- **不统一执行语义**：deposit、六项承诺、Storage2State、announced hash、交易来源差异全部保留（§2.1）。
- **实现偏差（相对 §5 的"零改动"）**：OpScheduler ctor 加第 8 参 `ioServicePool`（必填），Initializer + 3 个测试 fixture 构造点需更新（共享调度器的 GC 需要 io pool）。
