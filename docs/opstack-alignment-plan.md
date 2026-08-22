# OP Stack 执行流程对齐到以太坊执行流程 — 完整方案(路线图)

> 状态:2026-08-11 启动。分支 `worktree-op-alignment`(基于 feat-op-executor-e2e @ 0ba5256e0)。
> 当前实施:**层次 2(OP scheduler 适配器)已完成**(2026-08-11,见 `docs/opstack-scheduler-adapter-design.md`)。
> 本文记录完整对齐方案的后续路线图(层次 3 失败回执为共识风险,建议不实施)。

## 背景:两套流程现状

**以太坊流程(executor_version=2)**:
```
BlockSync/sealer → Scheduler(并行/串行) → TransactionExecutor concept
  每交易: createExecuteContext → prepare → execute → finish
  并行(SchedulerParallelImpl)/chunk 批处理(SchedulerSerialImpl)
  校验失败 → 失败回执(块继续)
  块级: finalizeBlock(奖励/提款)
```

**OP Stack 流程(executor_version=3+)**:
```
op-node/sequencer → engine newPayload → EngineServiceImpl OP 分支
  → OpSchedulerImpl::executeOpBlock → processOpBlock(整块逐交易串行循环)
  普通 tx 校验失败 → 整块 throw(对齐 op-geth Process 拒块)
  无失败回执机制
```

## 已识别的 OP 流程问题(对比以太坊)

1. **OP 无 scheduler 槽位**:MultiVersionScheduler 数组 {SchedulerManager, baseline, ethereum},OP 版本饱和到 v2 ethereum scheduler。块执行走 engine API executeOpBlock(不经 MultiVersionScheduler),但 RPC eth_call/getPendingStorage、txpool 校验走 m_scheduler → 用 ethereum scheduler,与 OP 语义分裂。
2. **OP 漏传 ledger**:Initializer.cpp:485 没传 ledger(对比 ethereum :414 传了)→ m_ledger=nullptr,本地构建 payload 落盘失效。
3. **无 RPC blockNumber 通知**:只有 baseline + ethereum 的 notifier,OP 无。
4. **无并行/chunk**:processOpBlock 整块串行,无 scheduler 并行能力。
5. **无失败回执**:普通 tx 校验失败整块拒(与 FISCO 失败回执体系不同,但**对齐 op-geth,是正确 OP 语义**)。

## 完整对齐方案(三层次)

### 层次 1:最小对齐(当前实施,~1-2 天)

解决"OP 游离于调度体系"的结构问题,不动执行路径、不碰共识语义。

- **A. MultiVersionScheduler OP 槽位**:仿 ethereumParallelScheduler 建 OP scheduler holder,加入数组,OP 版本不再饱和到 v2。
- **B. RPC blockNumber 通知**:仿 `m_setEthereumSchedulerBlockNumberNotifier` 加 OP 的。
- **C. eth_call/getPendingStorage 走 OP 语义**:修复 RPC 与 OP 块执行语义分裂。

验证:t8n 33 向量 + e2e 63 用例全绿(语义不变)。

### 层次 2:调度对齐(2026-08-11 设计定稿,详见 `docs/opstack-scheduler-adapter-design.md`)

让 OP 模式的 RPC eth_call 走 OP 语义 + OP 调度器成为调度体系一等公民。

- **`OpCallScheduler` 适配器**(新类,SchedulerInterface):`call()` 走 OpstackExecutor 真实注入
  (fee/blockGasLeft/chainId/blockHashes),块执行方法响亮抛错(替代静默饱和)。
- **MultiVersionScheduler 槽位**:数组 3→4,版本 3 不再饱和到 ethereum。
- **装配**:补传 ledger(修问题 2)+ OP holder + blockNumber 通知(EngineService VALID 分支触发)。
- **明确不做并行/chunk**:agent 核查证明 op-geth 串行、BaselineScheduler 承载不了 OP 块语义,
  processOpBlock 保留(问题 4 从层次 2 移除)。
- **保留整块拒块语义**(不引入失败回执)。
- OpstackExecutor 生产接入(当前生产零引用,只有测试)。

### 层次 3:完整对齐(后续评估,~8-12 天)

在层次 2 基础上 + 失败回执机制。

- **⚠️ 失败回执对齐是共识风险**:OP 对齐 op-geth 是"整块拒块",改成 FISCO 失败回执会**破坏 op-geth 对拍共识**(63 用例/33 向量会红)。
- **建议不做**这一项,除非未来 OP 生态明确需要 FISCO 风格失败回执。

## 关键文件

- `libinitializer/Initializer.cpp`(OP composition root,L455-497 + ethereum 对照 L389-415)
- `libinitializer/MultiVersionScheduler.{h,cpp}`(版本饱和)
- `libinitializer/EngineServiceInitializer.h`(build 签名,ledger 参数)
- `opstack-executor/OpstackExecutor.h`(concept 实现,层次 2 的生产接入点)
- `opstack-executor/OpBlockExecute.cpp`(processOpBlock,层次 2 编排替代)

## 决策记录

- 2026-08-11:用户选定**最小对齐**先做,并记录完整对齐方案作为后续路线图。
- 失败回执对齐(B 项):**不实施**(会破坏 op-geth 对拍)。
