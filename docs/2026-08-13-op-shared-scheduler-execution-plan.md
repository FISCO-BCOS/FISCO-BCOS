# OP 块执行经共享调度器（方案 B）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 OP 的逐交易执行从 `runOpBlockInjection` 切到共享的 `SchedulerSerialImpl`（串行模式），通过 per-executor `BlockContext` 携带 OP 状态，块级结果由抽出的 `finalizeOpBlockResult` 计算。

**Architecture:** OpScheduler 保持独立编排（m_pending/commit/verify），只改 `execute()` 内层：建 `OpBlockExecutionContext` → `SchedulerSerialImpl(serial=true).executeBlock(..., ctx)`（逐 tx 走 `createExecuteContext→prepare→execute→finish`）→ `finalizeOpBlockResult`。`runOpBlockInjection` 删除，OpScheduler 成为唯一执行入口。

**Tech Stack:** C++20 concepts/requires, tbb::parallel_pipeline, coroutines（bcos::task）, evmone, opstack-executor, transaction-scheduler。

## Global Constraints

- **前提：OP 只用线性执行**——SchedulerSerialImpl 必须用串行模式（`max_tokens=1` 且 `chunk=1`），绝不用 SchedulerParallelImpl。OP 装配路径加 `assert(m_serial)`。
- **BaselineScheduler.h 零改动**——共享接口的新参数必须带默认值，5 参调用仍有效。
- **ethereum 零漂移**——SchedulerSerialImpl 串行模式默认关闭（默认构造参数保持现行为）；EthereumExecutor 不改（`BlockContextOf` SFINAE 默认 EmptyBlockContext）。
- **concept 保持 5 参检查**——6 参是默认参扩展，不要求所有 TransactionExecutor 实现定义 BlockContext（用 SFINAE 默认 `BlockContextOf<E>`）。
- **错误面不变**——OpConsensusError/OpStorageError 保真重抛 + `catch(...)` RTTI-bypass 归一，映射 OpConsensusRejected/OpStorageFault/UnknownError。**ExecuteContext 内把 `OpTxValidationFailed` 归一为 `OpConsensusError`**（M1）。
- **块级三段式**——runOpBlockInjection 拆成块前（system_call + deposit-first/Jovian 校验 + hashes 构造）+ 逐笔（SchedulerSerialImpl）+ 块后（finalizeOpBlockResult）。三段都必须在 OpScheduler::execute() 内，不可遗漏块前职责（H2/M2）。
- **fee 惰性加载**——`loadOpFeeParams` 必须在本块 L1 attributes deposit 执行后读（H1），不可在块首快照。hashes/chainId 必须注入 BlockContext（H3）。
- **cumulativeGasUsed 回填**——finish() 内顺序累积并 `setCumulativeGasUsed`（H4），否则 receiptsRoot 错。
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
      std::vector<bcos::bytes> const& rawTxBytes, int64_t cumulative,
      std::optional<std::string> const& hashErr);
  ```
  - 行为 = 现 `runOpBlockInjection` 第 285-310 行的块级收尾（注意是 **285** 起，非 282——282 仍在逐笔循环内）：
    1. **从 rawTxBytes[i][0] 重建 txTypes**（镜像 225-282 循环的分类逻辑，`sealOpBlock` 的 `encodeReceiptForRoot` 需要每笔 type 字节）；
    2. `executor.finalizeBlock(view, header, ledgerConfig)` → MessagePasser 快照（Storage2State bridge）→ `sealOpBlock(result, cfg, mpStorage)` → `stateRootOf(bridge)` → `computeOpTxRoot(rawTxBytes)`；
    3. **hashErr 检查**（非空 → `throw OpStorageError("block-hash lookup failed: ...")`）；
    4. 返回 `OpExecuteBlockResult{receipts, seal, stateRoot, gasUsed, txRoot}`。
  - **不回填 cumulativeGasUsed**（H4 唯一 owner = ExecuteContext::finish，见 Task 3）；`narrowGasUsed`/`hexCumulative` 已搬到 OpCommon.h。

- [ ] **Step 1: 抽函数（先不删 runOpBlockInjection 的旧代码）**

在 `opstack-executor/OpBlockExecute.h` 新增 `finalizeOpBlockResult`（把现 runOpBlockInjection 285-310 行的逻辑整体搬入，形参改为函数参数，并补 txTypes 重建 + hashErr 检查；**不回填 cumulativeGasUsed**——回填在循环内，不在尾部）。`runOpBlockInjection` 仍保留原样（Task 5 才删），先不改它。

- [ ] **Step 2: 构建验证不破坏**

Run: `cmake --build build --target opstack-executor -j8`
Expected: 编译通过（新增函数未使用不报错，头文件内联模板无链接问题）。

- [ ] **Step 3: 让 runOpBlockInjection 改用 finalizeOpBlockResult**

把 `runOpBlockInjection` 尾部（285-310）替换为 `return finalizeOpBlockResult(executor, view, header, ledgerConfig, cfg, result.receipts, rawTxBytes, cumulative, hashErr);`——注意 `executor.finalizeBlock` 被调用一次即可。finalizeOpBlockResult **内部从 rawTxBytes 重建 txTypes**（runOpBlockInjection 旧循环的 txTypes 填充随尾部一起移入），并检查 hashErr；**不回填 cumulativeGasUsed**（回填在 runOpBlockInjection 循环内 236/280 行，保留不动）。

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

改 `SchedulerSerialImpl`：成员 `size_t m_chunkSize`、`bool m_serial`。`executeBlock` 里：
- `size_t chunk = m_serial ? 1 : (m_chunkSize == 0 ? max(count/mc, MIN_TRANSACTION_GRAIN_SIZE) : m_chunkSize);`（**serial 模式自身强制 chunk=1**，不依赖调用方传 chunkSize=1，防误用——B4）；
- `tbb::parallel_pipeline(m_serial ? 1 : MIN_TRANSACTION_GRAIN_SIZE, ...)`（serial 时 max_tokens=1）。

- [ ] **Step 3: executeBlock 加 6th 默认参 + 透传 createExecuteContext**

`executeBlock` 加 `BlockContextOf<TransactionExecutor>::type const& ctx = {}`。`createExecuteContext` 调用处用 `if constexpr (requires { executor.createExecuteContext(storage, blockHeader, transactions[i], i, ledgerConfig, false, ctx); })` 传带 ctx 的 7 参，否则传原 6 参（兼容无 BlockContext 的执行器/测试 executor）。

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
  // OpstackExecutor 内（OpBlockExecutionContext 字段完整——审查修正，非 {fee, blockGasLeft} 简化版）
  struct OpBlockExecutionContext {
      mutable bcos::evm::opstack::OpFeeParams fee;  // 可变：惰性加载 + DA scalar 覆盖（H1/H1c）
      mutable bool feeLoaded = false;               // fee 惰性加载标志（H1）
      mutable int64_t blockGasLeft;                 // 可变：逐 tx 递减
      mutable int64_t cumulativeGasUsed = 0;        // 可变：跨 tx 累积（H4）
      mutable bool seenNonDeposit = false;          // 可变：deposit-after-non-deposit 门（M2）
      evmone::state::BlockHashes* blockHashes;      // 只读：块级构造一次（H3）
      uint64_t chainId;                             // 只读：常量（H3）
      std::optional<uint16_t> daFootprintGasScalar; // 只读：preBlockOpSteps 算（H1c）
  };
  using BlockContext = OpBlockExecutionContext;

  template <class Storage>
  task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
      protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
      int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
      BlockContext const& blockCtx);   // const& —— 可变字段标 mutable，允许 const 下修改
  // ExecuteContext 增成员 `BlockContext const* m_ctx`；prepare/execute/finish 用它（改 mutable 字段）。
  ```

- [ ] **Step 1: 定义 OpBlockExecutionContext + BlockContext 别名**

在 `OpstackExecutor.h` 加 `OpBlockExecutionContext`（8 字段，前 5 个 mutable，见 Interfaces）+ `using BlockContext = OpBlockExecutionContext`。

- [ ] **Step 2: createExecuteContext 加 7th 参 + ExecuteContext 存 context**

`createExecuteContext` 加 `BlockContext const& blockCtx`（const&，可变字段标 mutable），ExecuteContext 存 `BlockContext const* m_ctx`。

- [ ] **Step 3: ExecuteContext::prepare() 加 deposit 分流 + fee 惰性 + DA scalar + seenNonDeposit + 归一**

```cpp
task::Task<void> prepare() {
    if (transaction.isDepositTx()) {
        if (m_ctx->seenNonDeposit)                                    // M2 顺序门
            throw bcos::evm::engine::OpConsensusError("op block: deposit after non-deposit");
        m_deposit = OpstackExecutor::depositFromTransaction(transaction);
        co_return;                                                     // deposit 无 opValidate
    }
    m_ctx->seenNonDeposit = true;
    if (!m_ctx->feeLoaded) {                                           // H1 fee 惰性加载
        eth::StorageStateView<Storage> stateView(storage);
        m_ctx->fee = op::loadOpFeeParams(stateView);
        if (m_ctx->daFootprintGasScalar)                               // H1c DA scalar 覆盖
            m_ctx->fee.da_footprint_gas_scalar = *m_ctx->daFootprintGasScalar;
        m_ctx->feeLoaded = true;
    }
    try {                                                              // M1 归一
        m_props = co_await executor.m_prepare(storage, blockHeader, transaction, ledgerConfig,
            m_ctx->fee, m_ctx->blockGasLeft);
    } catch (const OpTxValidationFailed& e) {
        throw bcos::evm::engine::OpConsensusError(
            std::string("runOpBlockInjection: normal tx validation failed: ") + e.what());
    }
}
```

- [ ] **Step 4: ExecuteContext::execute()/finish() 支持 deposit（executeDeposit）+ hashes/chainId + cumulativeGasUsed**

```cpp
task::Task<void> execute() {
    if (transaction.isDepositTx()) {
        // 调 executeDeposit 成员（非 op::runDeposit 自由函数）；内部 applyStateDiff
        m_receipt = co_await executor.executeDeposit(storage, blockHeader, m_deposit,
            m_ctx->chainId, m_ctx->blockGasLeft, ledgerConfig, m_ctx->blockHashes);
    } else {
        m_receipt = co_await executor.m_execute(storage, blockHeader, transaction, ledgerConfig,
            m_props, m_diff, m_ctx->chainId, m_ctx->blockGasLeft, m_ctx->blockHashes);  // H3
    }
}
task::Task<protocol::TransactionReceipt::Ptr> finish() {
    protocol::TransactionReceipt::Ptr receipt;
    if (transaction.isDepositTx()) {
        receipt = m_receipt;              // executeDeposit 已 applyStateDiff，不再 apply
    } else {
        receipt = co_await executor.m_finish(storage, blockHeader, ledgerConfig, m_receipt, m_diff);
    }
    // H4：唯一 owner，累积 + 回填 + 递减（narrowGasUsed/hexCumulative 已在 OpCommon.h）
    auto gasUsed = op::narrowGasUsed(receipt->gasUsed());
    m_ctx->cumulativeGasUsed += gasUsed;
    receipt->setCumulativeGasUsed(op::hexCumulative(m_ctx->cumulativeGasUsed));
    m_ctx->blockGasLeft -= gasUsed;
    co_return receipt;
}
```
**前置**：把 `narrowGasUsed` + `hexCumulative` 从 OpBlockExecute.h **搬到 OpCommon.h**（OpstackExecutor.h 不能 include OpBlockExecute.h，否则循环包含——OpstackExecutorTest.cpp 只 include OpstackExecutor.h 会编译失败）。

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

- [ ] **Step 1: OpScheduler ctor 加 ioServicePool 参（必填）**

`OpScheduler(receiptFactory, hashImpl, chainId, forkTimestamps, blockFactory, multiLayerStorage, ledger, ioServicePool)`——**必填，无默认 nullptr**（M4：GC 的 `m_ioServicePool->getIOService()` 空指针会崩，必填让编译器强制所有构造点显式传）。存成员 `m_ioServicePool`。

- [ ] **Step 2: execute() 改为三段式（块前 → SchedulerSerialImpl → 块后）**

替换 `execute()` 内 `runOpBlockInjection` 调用为：
```cpp
// ① 块前步骤（preBlockOpSteps，确定函数——Task 5 抽成，此处先内联同义代码）
//    deposits 向量保留（预解码），供 deposit-first 校验 + DA scalar 用
auto blk = detail::toBlockInfo(header);
std::optional<std::string> hashErr;
detail::RecentBlockHashes<ViewType> hashes(view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
eth::StorageStateView<ViewType> stateView(view);
auto sysDiff = evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());  // H2
bcos::task::syncWait(eth::applyStateDiff(view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *m_hashImpl));
// deposit-first 检查（首 tx 必须 L1 attributes，用 deposits[0]）+ Jovian shape（镜像 runOpBlockInjection 180-211 行）
// DA scalar（H1c）：Jovian 从 deposits[0].data[176:178] 提取大端 uint16 存入 daFootprintGasScalar
std::optional<uint16_t> daFootprintGasScalar = /* 照抄 runOpBlockInjection 251-260 */;

// ② 建 context（fee 不在此加载——惰性，见 Task 3 Step 3）
OpBlockExecutionContext ctx{
    .blockGasLeft = static_cast<int64_t>(header.gasLimit()),
    .blockHashes = &hashes, .chainId = m_chainId,
    .daFootprintGasScalar = daFootprintGasScalar};

// ③ 逐笔（串行调度器，每块新建）
SchedulerSerialImpl serialScheduler(m_ioServicePool, /*chunkSize=*/1, /*serial=*/true);
auto receipts = co_await serialScheduler.executeBlock(
    view, executor, header, transactions, execLedgerConfig, ctx);

// ④ 块后收尾（hashErr 检查在 finalizeOpBlockResult 内部）
auto result = bcos::evm::engine::finalizeOpBlockResult(
    executor, view, header, execLedgerConfig, cfg, receipts, rawTxBytes,
    ctx.cumulativeGasUsed, hashErr);
```
（`executor` 为块内新建的 OpstackExecutor；`cfg` 来自 `op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps)`——**configAt 是双参**，补 `m_forkTimestamps`；`execLedgerConfig` 只带 evmcRevision；`deposits` 向量由 execute() 预解码保留。）

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
- Consumes: `finalizeOpBlockResult`（Task 1）+ `preBlockOpSteps`（Task 4 Step 2 里"镜像 runOpBlockInjection 180-211 行"的块前步骤，若 Task 4 是内联则此处抽成函数）。
- Produces: `runOpBlockInjection` 不再存在；`executeDeposit` 保留（eth_call 用）。

- [ ] **Step 1: 删 runOpBlockInjection + 抽 preBlockOpSteps + 过渡期等价测试**

删除 `OpBlockExecute.h` 的 `runOpBlockInjection` 整个函数——它已拆成 `preBlockOpSteps`（块前）+ 逐笔循环（SchedulerSerialImpl）+ `finalizeOpBlockResult`（Task 1）。把 168-211 行（hashes 构造 + system_call + deposit-first/Jovian 校验 + DA scalar）抽成确定函数供 Task 4 复用：
```cpp
// 输出 hashes / hashErr / daFootprintGasScalar 为引用参数
template <class Storage>
void preBlockOpSteps(Storage& view, protocol::BlockHeader const& header,
    OpForkConfig const& cfg, std::vector<bcos::bytes> const& rawTxBytes,
    std::vector<op::DepositTx> const& deposits, OpstackExecutor& executor,
    crypto::Hash::Ptr const& hashImpl, detail::RecentBlockHashes<Storage>& hashes,
    std::optional<std::string>& hashErr, std::optional<uint16_t>& daFootprintGasScalar);
```
删除 Task 4 的过渡期等价测试（route B 消失）。

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

- [ ] **Step 1: 确认设计文档与实现一致**

设计文档已在审查后修订（spec §4.1 SFINAE、§4.3 fee 惰性 + H3/H4、§4.4 三段式、§6 M1、§9 ioServicePool 偏差）。实现完成后通读 `docs/2026-08-13-op-shared-scheduler-execution-design.md`，确认无与实现不符的遗留（尤其 §5 的"Initializer 零改动"已改为"例外"）。

- [ ] **Step 2: 全量构建 + 测试**

Run: `cmake --build build --target opstack-executor opstack-executor-tests opstack-executor-block-tests opstack-executor-detail-tests test-transaction-scheduler libinitializer -j8`
Expected: 全绿。

- [ ] **Step 3: Commit**

```bash
git add docs/2026-08-13-op-shared-scheduler-execution-design.md
git commit -m "docs(opstack): 方案 B 实现后偏差确认（与实现对齐）" --no-verify
```
