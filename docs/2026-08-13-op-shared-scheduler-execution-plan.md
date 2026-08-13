# OP 块执行经共享调度器（方案 B）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 OP 的逐交易执行从 `runOpBlockInjection` 切到共享的 `SchedulerSerialImpl`（串行模式），通过 per-executor `BlockContext` 携带 OP 状态，块级结果由抽出的 `finalizeOpBlockResult` 计算。

**Architecture:** OpScheduler 保持独立编排（m_pending/commit/verify），只改 `execute()` 内层：建 `OpBlockExecutionContext` → `SchedulerSerialImpl(serial=true).executeBlock(..., ctx)`（逐 tx 走 `createExecuteContext→prepare→execute→finish`）→ `finalizeOpBlockResult`。`runOpBlockInjection` 删除，OpScheduler 成为唯一执行入口。

**Tech Stack:** C++20 concepts/requires, tbb::parallel_pipeline, coroutines（bcos::task）, evmone, opstack-executor, transaction-scheduler。

## Global Constraints

- **前提：OP 只用线性执行**——SchedulerSerialImpl 必须用串行模式（`max_tokens=1` 且 `chunk=1`），绝不用 SchedulerParallelImpl。
- **BaselineScheduler.h 零改动**——共享接口的新参数必须带默认值，5 参调用仍有效。
- **ethereum 零漂移**——SchedulerSerialImpl 串行模式默认关闭（默认构造参数保持现行为）；EthereumExecutor 不改。
- **concept 保持 5 参检查**——6 参是默认参扩展，不要求所有 TransactionExecutor 实现定义 BlockContext（用 SFINAE 默认 `BlockContextOf<E>`）。
- **错误面不变**——OpConsensusError/OpStorageError 保真重抛 + `catch(...)` RTTI-bypass 归一，映射 OpConsensusRejected/OpStorageFault/UnknownError。
- 真实测试命令：`cmake --build build --target opstack-executor-tests opstack-executor-block-tests opstack-executor-detail-tests test-transaction-scheduler -j8`；二进制在 `build/opstack-executor/tests/` 和 `build/transaction-scheduler/tests/`。

---

### Task 1: 抽取 finalizeOpBlockResult

**Files:**
- Modify: `opstack-executor/OpBlockExecute.h`（抽函数 + runOpBlockInjection 改用）
- Test: `opstack-executor/tests/OpDualPathEquivalenceTest.cpp`（现状已覆盖，作为回归）

**Interfaces:**
- Produces:
  ```cpp
  // opstack-executor/OpBlockExecute.h，namespace bcos::evm::engine
  template <class Storage>
  OpExecuteBlockResult finalizeOpBlockResult(OpstackExecutor& executor, Storage& view,
      protocol::BlockHeader const& header, ledger::LedgerConfig const& ledgerConfig,
      OpForkConfig const& cfg, std::vector<protocol::TransactionReceipt::Ptr> const& receipts,
      std::vector<bcos::bytes> const& rawTxBytes, int64_t cumulative);
  ```
  - 行为 = 现 `runOpBlockInjection` 第 282-310 行的块级收尾：txTypes 循环 → `executor.finalizeBlock(view, header, ledgerConfig)` → `result.gasUsed = cumulative` → MessagePasser 快照（Storage2State bridge）→ `sealOpBlock(result, cfg, mpStorage)` → `stateRootOf(bridge)` → `computeOpTxRoot(rawTxBytes)` → 返回 `OpExecuteBlockResult{receipts, seal, stateRoot, gasUsed, txRoot}`。

- [ ] **Step 1: 抽函数（先不删 runOpBlockInjection 的旧代码）**

在 `opstack-executor/OpBlockExecute.h` 新增 `finalizeOpBlockResult`（把现 runOpBlockInjection 282-310 行的逻辑整体搬入，形参改为函数参数）。`runOpBlockInjection` 仍保留原样（Task 5 才删），先不改它。

- [ ] **Step 2: 构建验证不破坏**

Run: `cmake --build build --target opstack-executor -j8`
Expected: 编译通过（新增函数未使用不报错，头文件内联模板无链接问题）。

- [ ] **Step 3: 让 runOpBlockInjection 改用 finalizeOpBlockResult**

把 `runOpBlockInjection` 尾部（282-310）替换为 `return finalizeOpBlockResult(executor, view, header, ledgerConfig, cfg, result.receipts, rawTxBytes, cumulative);`——注意 `executor.finalizeBlock` 被调用一次即可，`result.txTypes` 已在循环里填好，finalizeOpBlockResult 内部重建 txTypes。

- [ ] **Step 4: 跑 block 测试确认行为不变**

Run: `./build/opstack-executor/tests/opstack-executor-block-tests`
Expected: 全绿（dual-path route A==B 仍等价，golden 仍匹配）。

- [ ] **Step 5: Commit**

```bash
git add opstack-executor/OpBlockExecute.h
git commit -m "refactor(opstack): 抽取 finalizeOpBlockResult（块级收尾公共函数，runOpBlockInjection 改用）" --no-verify
```

---

### Task 2: SchedulerSerialImpl 串行模式 + 默认 BlockContext 参

**Files:**
- Modify: `transaction-scheduler/bcos-transaction-scheduler/SchedulerSerialImpl.h`
- Test: `transaction-scheduler/tests/`（新增或复用——用 TestEthereumExecutorScheduler 的模式建 ioServicePool）

**Interfaces:**
- Consumes: `BlockContextOf<E>`（本任务定义，SFINAE 默认 `EmptyBlockContext`）。
- Produces:
  ```cpp
  // bcos-framework 或 transaction-scheduler 头（namespace bcos::scheduler_v1）
  struct EmptyBlockContext {};

  // SFINAE：执行器定义了自己的 BlockContext 就用它，否则回退 EmptyBlockContext
  template <class E, class = void>
  struct BlockContextOf { using type = EmptyBlockContext; };
  template <class E>
  struct BlockContextOf<E, std::void_t<typename E::BlockContext>> { using type = typename E::BlockContext; };

  // SchedulerSerialImpl 构造：chunkSize=0→现公式；serial=true→chunk=1 + max_tokens=1
  explicit SchedulerSerialImpl(IOServicePool::Ptr pool, std::size_t chunkSize = 0, bool serial = false);

  // executeBlock 加 6th 默认参（concept 5 参调用仍有效）
  template <class Storage, executor_v1::TransactionExecutor<Storage> TransactionExecutor>
  task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage& storage,
      TransactionExecutor& executor, protocol::BlockHeader const& blockHeader,
      ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig,
      typename BlockContextOf<TransactionExecutor>::type const& ctx = {});
  ```

- [ ] **Step 1: 定义 EmptyBlockContext + BlockContextOf**

在 `transaction-scheduler/bcos-transaction-scheduler/SchedulerSerialImpl.h`（或公共头）顶部加 `EmptyBlockContext` 结构 + `BlockContextOf` SFINAE 模板。

- [ ] **Step 2: 构造参数化 chunkSize/serial**

改 `SchedulerSerialImpl`：成员 `size_t m_chunkSize`、`bool m_serial`。`executeBlock` 里 chunk 计算改为 `m_chunkSize == 0 ? max(count/mc, MIN_TRANSACTION_GRAIN_SIZE) : m_chunkSize`；`tbb::parallel_pipeline(m_serial ? 1 : MIN_TRANSACTION_GRAIN_SIZE, ...)`（serial 时 max_tokens=1）。

- [ ] **Step 3: executeBlock 加 6th 默认参 + 透传 createExecuteContext**

`executeBlock` 加 `BlockContextOf<TransactionExecutor>::type const& ctx = {}`。`createExecuteContext` 调用处用 `if constexpr (requires { executor.createExecuteContext(storage, blockHeader, transactions[i], i, ledgerConfig, false, ctx); })` 传 6 参，否则传 5 参（兼容无 BlockContext 的执行器/测试 executor）。

- [ ] **Step 4: 写串行等价测试（红）**

在 `transaction-scheduler/tests/` 新增 `TestSchedulerSerialMode.cpp`（或并入现有测试文件）：对同一简单交易序列（2-3 条独立 tx），断言默认 pipeline 与 `serial=true, chunkSize=1` 产生相同 receipts。
Expected: 先红（serial 模式尚未生效/编译不过）。

- [ ] **Step 5: 跑测试转绿**

Run: `cmake --build build --target test-transaction-scheduler -j8 && ./build/transaction-scheduler/tests/test-transaction-scheduler`
Expected: 全绿（含新串行等价测试）。

- [ ] **Step 6: Commit**

```bash
git add transaction-scheduler/bcos-transaction-scheduler/SchedulerSerialImpl.h transaction-scheduler/tests/
git commit -m "feat(scheduler): SchedulerSerialImpl 串行模式（chunkSize/serial 构造参）+ executeBlock 6th BlockContext 默认参" --no-verify
```

---

### Task 3: OpstackExecutor 的 BlockContext + ExecuteContext 生命周期改造

**Files:**
- Modify: `opstack-executor/OpstackExecutor.h`
- Test: `opstack-executor/tests/OpstackExecutorTest.cpp`

**Interfaces:**
- Consumes: `BlockContextOf` / `EmptyBlockContext`（Task 2）；`finalizeOpBlockResult`（Task 1，本任务不用，Task 4 用）。
- Produces:
  ```cpp
  // OpstackExecutor 内
  struct OpBlockExecutionContext { bcos::evm::opstack::OpFeeParams fee; int64_t blockGasLeft; };
  using BlockContext = OpBlockExecutionContext;

  template <class Storage>
  task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
      protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
      int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
      BlockContext const& blockCtx = {});
  // ExecuteContext 增成员 `BlockContext const* m_ctx`；prepare/execute/finish 用它。
  ```

- [ ] **Step 1: 定义 OpBlockExecutionContext + BlockContext 别名**

在 `OpstackExecutor.h` 加 `OpBlockExecutionContext{fee, blockGasLeft}` + `using BlockContext = OpBlockExecutionContext`。

- [ ] **Step 2: createExecuteContext 加 6th 参 + ExecuteContext 存 context**

`createExecuteContext` 加 `BlockContext const& blockCtx = {}`，ExecuteContext 存 `m_ctx`（`contextID` 已存，可加 `m_ctx` 指针）。

- [ ] **Step 3: ExecuteContext::prepare() 加 deposit 分流 + context 读取**

```cpp
task::Task<void> prepare() {
    if (transaction.isDepositTx()) {
        auto dep = OpstackExecutor::depositFromTransaction(transaction);   // 自建
        m_props = buildDepositProps(dep);                                    // 跳过 opValidate
    } else {
        m_props = co_await executor.m_prepare(storage, blockHeader, transaction, ledgerConfig,
            m_ctx ? m_ctx->fee : OpFeeParams{}, m_ctx ? m_ctx->blockGasLeft : 0);
    }
}
```
`buildDepositProps` 为本任务新增私有 helper（deposit 的 props：chainId/gasLimit 等，从 DepositTx 构建——参照 `executeDeposit` 现有逻辑）。

- [ ] **Step 4: ExecuteContext::execute()/finish() 支持 deposit + 递减 gasLeft**

`execute()`：`if (transaction.isDepositTx())` 走 deposit transition（参照 `executeDeposit` 的 opTransition 段），否则走现有 `m_execute`。`finish()`：现有 `m_finish`（apply diff）+ 若 `m_ctx` 则 `m_ctx->blockGasLeft -= receipt->gasUsed()`。

- [ ] **Step 5: 写 deposit 生命周期测试（红）**

在 `OpstackExecutorTest.cpp` 新增：构造一个 deposit 型 Transaction（`isDepositTx()` 为真），经 `createExecuteContext→prepare→execute→finish` 执行，断言 receipt 正常返回 + `m_ctx->blockGasLeft` 递减。
Expected: 先红（deposit 分流未实现）。

- [ ] **Step 6: 跑测试转绿**

Run: `cmake --build build --target opstack-executor-tests -j8 && ./build/opstack-executor/tests/opstack-executor-tests`
Expected: 全绿（含新 deposit 生命周期用例）。

- [ ] **Step 7: Commit**

```bash
git add opstack-executor/OpstackExecutor.h opstack-executor/tests/OpstackExecutorTest.cpp
git commit -m "feat(opstack): ExecuteContext 生命周期支持 BlockContext（fee/gasLeft）+ deposit 分流" --no-verify
```

---

### Task 4: OpScheduler::execute() 切到 SchedulerSerialImpl

**Files:**
- Modify: `opstack-executor/OpScheduler.h`、`libinitializer/Initializer.cpp`、`opstack-executor/tests/OpSchedulerTest.cpp`、`opstack-executor/tests/OpDualPathEquivalenceTest.cpp`、`opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp`
- Test: 现有 block 测试（过渡期等价由 dual-path 守护）

**Interfaces:**
- Consumes: `SchedulerSerialImpl(serial=true)`（Task 2）、`OpBlockExecutionContext`（Task 3）、`finalizeOpBlockResult`（Task 1）。
- Produces: OpScheduler ctor 加第 8 参 `bcos::IOServicePool::Ptr ioServicePool`；`execute()` 改为经 SchedulerSerialImpl。

- [ ] **Step 1: OpScheduler ctor 加 ioServicePool 参**

`OpScheduler(receiptFactory, hashImpl, chainId, forkTimestamps, blockFactory, multiLayerStorage, ledger, ioServicePool = nullptr)`，存成员 `m_ioServicePool`。

- [ ] **Step 2: execute() 改为经 SchedulerSerialImpl**

替换 `execute()` 内 `runOpBlockInjection` 调用为：
```cpp
// 建 context
OpBlockExecutionContext ctx{
    .fee = op::loadOpFeeParams(bcos::executor_v1::eth::StorageStateView<ViewType>(view)),
    .blockGasLeft = static_cast<int64_t>(header.gasLimit())};
// 串行调度器（每块新建，或成员复用）
SchedulerSerialImpl serialScheduler(m_ioServicePool, /*chunkSize=*/1, /*serial=*/true);
auto receipts = co_await serialScheduler.executeBlock(
    view, executor, header, transactions, execLedgerConfig, ctx);
// 块级收尾
int64_t cumulative = 0;
for (auto const& r : receipts) cumulative += narrowGasUsed(r->gasUsed());
auto result = bcos::evm::engine::finalizeOpBlockResult(
    executor, view, header, execLedgerConfig, cfg, receipts, rawTxBytes, cumulative);
```
（`executor` 为块内新建的 OpstackExecutor；`cfg` 来自 `configAt(timestamp/1000)`；`execLedgerConfig` 只带 evmcRevision。）

- [ ] **Step 3: 更新 4 个构造点（Initializer + 3 测试 fixture）**

`Initializer.cpp` 传 `m_ioServicePool`；测试 fixture 建 `std::make_shared<bcos::IOServicePool>(1)`（或复用现有 io pool 模式）传入。

- [ ] **Step 4: 过渡期等价测试（红→绿）**

在 `OpSchedulerTest.cpp` 或 `OpDualPathEquivalenceTest.cpp` 加：同一块经 OpScheduler(serial SchedulerSerialImpl) 执行的结果，与直接 `runOpBlockInjection` 结果全等（receipts/stateRoot/txRoot/gasUsed）。这是 Task 5 删除 runOpBlockInjection 前的守护。
Expected: 先红（execute() 尚未切），实现后绿。

- [ ] **Step 5: 全量跑 opstack 测试**

Run: `cmake --build build --target opstack-executor-tests opstack-executor-block-tests opstack-executor-detail-tests -j8 && ./build/opstack-executor/tests/opstack-executor-tests && ./build/opstack-executor/tests/opstack-executor-detail-tests && ./build/opstack-executor/tests/opstack-executor-block-tests`
Expected: 全绿。

- [ ] **Step 6: Commit**

```bash
git add opstack-executor/OpScheduler.h libinitializer/Initializer.cpp opstack-executor/tests/
git commit -m "feat(opstack): OpScheduler::execute 切到 SchedulerSerialImpl 串行模式 + finalizeOpBlockResult" --no-verify
```

---

### Task 5: 删除 runOpBlockInjection + dual-path 改单路径

**Files:**
- Modify: `opstack-executor/OpBlockExecute.h`（删 runOpBlockInjection）、`opstack-executor/tests/OpDualPathEquivalenceTest.cpp`

**Interfaces:**
- Consumes: `finalizeOpBlockResult`（Task 1）——是 runOpBlockInjection 删除后唯一存留的块级收尾。
- Produces: `runOpBlockInjection` 不再存在；`executeDeposit` 保留（eth_call 用）。

- [ ] **Step 1: 删 runOpBlockInjection + 过渡期等价测试**

删除 `OpBlockExecute.h` 的 `runOpBlockInjection` 整个函数（保留 `finalizeOpBlockResult` + 共享 helper）。删除 Task 4 的过渡期等价测试（route B 消失）。

- [ ] **Step 2: dual-path 测试改单路径**

`OpDualPathEquivalenceTest.cpp` 删 route B（直接 runOpBlockInjection），保留 OpScheduler 路径 + golden 比对（`_op_expected` stateRoot）+ t8n 语料。

- [ ] **Step 3: 全量重验**

Run: `cmake --build build --target opstack-executor-tests opstack-executor-block-tests opstack-executor-detail-tests test-transaction-scheduler -j8` 然后全部二进制跑绿。
Expected: 全绿。`grep -r "runOpBlockInjection" --include="*.h" --include="*.cpp" .` 无残留（仅注释可留）。

- [ ] **Step 4: Commit**

```bash
git add opstack-executor/OpBlockExecute.h opstack-executor/tests/OpDualPathEquivalenceTest.cpp
git commit -m "refactor(opstack): 删除 runOpBlockInjection，OpScheduler 经 SchedulerSerialImpl 为唯一执行路径" --no-verify
```

---

### Task 6: 文档 + 收尾

- [ ] **Step 1: 更新设计文档**

在 `docs/2026-08-13-op-shared-scheduler-execution-design.md` 加"实现后偏差"小节：concept 保持 5 参检查（6 参默认扩展）；EthereumExecutor 不改（BlockContextOf SFINAE 默认）；OpScheduler ctor 加 ioServicePool（Initializer + 测试 fixture 构造点更新）。

- [ ] **Step 2: 全量构建 + 测试**

Run: `cmake --build build --target opstack-executor opstack-executor-tests opstack-executor-block-tests opstack-executor-detail-tests test-transaction-scheduler libinitializer -j8`
Expected: 全绿。

- [ ] **Step 3: Commit**

```bash
git add docs/2026-08-13-op-shared-scheduler-execution-design.md
git commit -m "docs(opstack): 方案 B 实现后偏差记录（concept 5 参检查 + ioServicePool ctor）" --no-verify
```
