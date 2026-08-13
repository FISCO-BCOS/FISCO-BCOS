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
| deposit | **方案 A**：deposit 执行逻辑**搬进 ExecuteContext 生命周期**（prepare 构建 DepositTx → execute transition → finish apply diff） | 与"统一生命周期"目标一致；不调用现成的 `executeDeposit`（避免 deposit 绕过生命周期） |
| runOpBlockInjection | **删除**（逐笔循环被 SchedulerSerialImpl 取代），块级收尾抽成 `finalizeOpBlockResult` | OpScheduler 成为唯一执行入口；dual-path 测试改单路径 |
| 错误面 | 保留在 OpScheduler 的 typed-catch（OpConsensusError/OpStorageError 保真 + RTTI-bypass 归一） | 与现状一致 |

## 3. 架构

```
                    OpScheduler（SchedulerInterface，编排不变）
                    executeBlock → coExecuteBlock（m_pending / commitPersist / verify / fastPath / 连续性）
                                          │  execute()（唯一改动）
                                          ▼
            BlockContext = OpBlockExecutionContext{fee, blockGasLeft(可变)}      ← per-executor 类型
                                          │
                                          ▼
            SchedulerSerialImpl（共享调度器，串行模式：max_tokens=1 + chunk=1）
            逐 tx：create → prepare → execute → finish（= 顺序 for 循环）
                                          │
                                          ▼
            OpstackExecutor::ExecuteContext（改造）
            prepare：isDepositTx() 分流（depositFromTransaction 自建）→ 读 context.fee/gasLeft
            execute / finish：deposit 与普通 tx 各走自己的 transition / apply diff
                                          │
                                          ▼
            finalizeOpBlockResult（从 runOpBlockInjection 抽出）
            finalizeBlock + MessagePasser + sealOpBlock + stateRootOf + computeOpTxRoot
```

## 4. 接口设计

### 4.1 per-executor BlockContext（共享接口不写死类型）

```cpp
// executor_v1::TransactionExecutor concept 新增要求：
typename TransactionExecutorType::BlockContext;   // 每个执行器声明自己的块上下文类型

// TransactionScheduler concept：
concept TransactionScheduler = requires(...,
    const typename TransactionExecutor::BlockContext& blockCtx) {
    { scheduler.executeBlock(storage, executor, blockHeader, transactions, ledgerConfig, blockCtx) }
        -> IsAwaitableReturnValue<vector<Receipt::Ptr>>;
};

// 各执行器声明自己的类型：
struct OpstackExecutor {
    using BlockContext = OpBlockExecutionContext;   // {fee, blockGasLeft}
    // ExecuteContext::prepare() 内部自己转换：OpBlockExecutionContext → m_props
};
struct EthereumExecutor {
    using BlockContext = bcos::scheduler_v1::EmptyBlockContext;   // 空，默认忽略
};
```

- 参数类型是 `TransactionExecutor::BlockContext`——**每个执行器一个类型**（"出入各种类型"）；
- 转换在**执行器内部**（ExecuteContext 读自己的 BlockContext → props）；
- 默认 `= {}` → 5 参调用（BaselineScheduler.h）仍有效，零改动；
- 各 `BlockContext` 需**默认可构造**（`OpBlockExecutionContext` 需可默认构造；`EmptyBlockContext` 天然可）。

### 4.2 SchedulerSerialImpl 串行模式

```cpp
class SchedulerSerialImpl {
    explicit SchedulerSerialImpl(IOServicePool::Ptr pool,
        std::size_t chunkSize = 0, bool serial = false);
    // chunkSize=0（默认）→ 现公式 max(count/max_concurrency, MIN_TRANSACTION_GRAIN_SIZE)
    // serial=true → max_tokens=1 且 chunk=1 → pipeline 退化为逐 tx 顺序循环
};
```

- ethereum 装配传默认（零漂移）；OP 传 `serial=true`。
- 必须**同时** `max_tokens=1` 和 `chunk=1`——单改 chunk 不够（见 §8.1）。

### 4.3 OpstackExecutor::ExecuteContext 改造

```cpp
ExecuteContext<Storage>::prepare() {
    if (transaction.isDepositTx()) {
        auto dep = OpstackExecutor::depositFromTransaction(*transaction);  // 自建，无需 deposits 向量
        m_props = buildDepositProps(dep);                                   // 跳过 opValidate（无签名）
    } else {
        m_props = executor.m_prepare(storage, header, tx, ledgerConfig, context.fee, context.blockGasLeft);
    }
}
// execute()：deposit → deposit transition；普通 tx → opTransition
// finish()：applyStateDiff + context.blockGasLeft -= receipt->gasUsed()   （串行下无竞态）
```

### 4.4 OpScheduler::execute() 数据流

```
建 context（loadOpFeeParams + blockGasLeft = header.gasLimit）
→ SchedulerSerialImpl(serial=true).executeBlock(view, executor, header, txs, ledgerConfig, ctx)
→ 返回按序 receipts
→ finalizeOpBlockResult(view, header, cfg, receipts, rawTxBytes, cumulative) → OpExecuteBlockResult
（累积 gasUsed 从 receipts 求和；rawTxBytes 从各 tx extraTransactionBytes 取）
```

## 5. 组件边界

### 删除
- `runOpBlockInjection`（整个函数）——逐笔循环被 SchedulerSerialImpl 取代；
- OpScheduler::execute() 里的旧循环；
- OpDualPathEquivalenceTest 的 route B（双路径 → 单路径）。

### 新增
- `OpBlockExecutionContext`（OpstackExecutor::BlockContext）：`{fee, blockGasLeft}`；
- `bcos::scheduler_v1::EmptyBlockContext`（ethereum 默认）；
- `finalizeOpBlockResult(view, header, cfg, receipts, rawTxBytes, cumulative)`；
- SchedulerSerialImpl 串行模式（构造参数 `chunkSize` + `serial`）；
- ExecuteContext 的 deposit 分流 + context 读取（`depositFromTransaction` 自建 DepositTx）。

### 保留（不变）
- OpScheduler 编排：coExecuteBlock/coCommitBlock、m_pending、commitPersist、fastPathHit、commitContinuityCheck、六路 verify；
- OpScheduler 纯虚：call/getCode/getABI/getPendingStorageAt（eth_call 走 `executeTransaction`，保留）；
- seam（OpSchedulerSeam）、引擎、Initializer 装配——零改动；
- `executeTransaction` 保留（eth_call 注入路径）；`executeDeposit` **保留**（eth_call 的 deposit 分支仍调它）；块级 deposit 走 ExecuteContext 生命周期（§4.3），transition 逻辑与 executeDeposit 共享可抽公共函数。

## 6. 错误处理

- 异常分类**保留在 OpScheduler**：`execute()` 用现有 typed-catch（OpConsensusError/OpStorageError 保真重抛 + `catch(...)` RTTI-bypass 归一），包住 SchedulerSerialImpl 调用 + finalizeOpBlockResult。错误面不变：OpConsensusRejected / OpStorageFault / UnknownError 映射不变。
- 串行模式下异常沿 `co_await` 链直接传播，无并发吞异常风险（实现时验证 tbb 不包装异常）。

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

### 8.3 deposit 生命周期化 + BlockContext 简化

ExecuteContext 直接 `depositFromTransaction(*transaction)` 自建 DepositTx，去掉现 execute() 里的预解码 deposits 向量 → BlockContext 缩为 `{fee, blockGasLeft}`。

## 9. 范围（明确不做）

- **不做并行**：OP 绝不走 SchedulerParallelImpl（前提）。
- **不改 ethereum 执行语义**：SchedulerSerialImpl 串行模式默认关闭，ethereum 装配零改动。
- **不引入骨架**：SchedulerSkeleton 不复活；OpScheduler 保持独立编排。
- **不统一执行语义**：deposit、六项承诺、Storage2State、announced hash、交易来源差异全部保留（§2.1）。
