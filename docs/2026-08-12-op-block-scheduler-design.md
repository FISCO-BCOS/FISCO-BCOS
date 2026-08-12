# OpBlockScheduler — OP 统一调度器设计（块执行 + eth_call + commit）

> 状态：2026-08-12 设计定稿（brainstorming + 用户 3 决策确认）。分支 `feat-op-block-scheduler`（基址 feat-op-executor-e2e `0ba5256e0`）。
> 前置：`docs/opstack-scheduler-adapter-design.md`（§0 已判定 processOpBlock 保留、BaselineScheduler 承载不了 OP 块语义、唯一 RPC 语义分裂是 call()）、`docs/op-eth-executor-unification-design.md`（执行层统一，processOpBlock 不动）。

## 1. 背景与问题

FISCO OP 模式当前两条执行路：
- **块执行**：engine newPayload 直连 `OpSchedulerImpl::executeOpBlock`（→ processOpBlock），不实现 `SchedulerInterface`。
- **eth_call**：OP 适配器 `OpCallScheduler` 在并行分支（op-alignment/op-eth-executor-unify），**未合入 executor-e2e**（当前基址没有 eth_call 路径）。

与 ethereum（`BaselineScheduler<EthereumExecutor>` 统一驱动块执行 + eth_call）不对称：OP 块执行是 engine 直连、eth_call 缺失/分离。

**方案 B**：新建 `OpBlockScheduler`——实现 `SchedulerInterface` 的 OP 统一调度器，块执行 + eth_call + commit 合并，OP 在调度体系成为一等公民（slot 3）。

## 2. 目标与范围

### 2.1 目标
- OP 块执行 + eth_call + commit 统一进 `OpBlockScheduler`（SchedulerInterface）。
- OP 在 MultiVersionScheduler 成为 slot 3 一等公民。
- eth_call 走 OP 语义（L1Block fee + vault 路由），不再缺失。

### 2.2 范围
1. 新建 `OpBlockScheduler`（opstack-executor/）：`executeBlock`（封装 executeOpBlock 逻辑）+ `call`（eth_call，重建适配器）+ `commitBlock`（块落盘 + **head 推进 #19**）+ 存储读 + 拒绝非 OP 语义。
2. engine `handleOpNewPayload` 改调 `scheduler.executeBlock`（SFINAE probe 适配）。
3. MultiVersionScheduler slot 3 → `OpBlockScheduler` + Initializer 装配。
4. `OpSchedulerImpl`/`OpCallScheduler` 去留（executeOpBlock 逻辑被复用）。
5. 等价重验：t8n 77 + 引擎门 65 + 全套件 + enhanced-corpus 无效分类。

### 2.3 明确不做（YAGNI）
- 不改 processOpBlock 块级编排（blockGasLeft 递减在内部，不动）。
- 不迁 engine 分类 barrier（handleOpNewPayload 的 INVALID/-32603 分类留 engine）。
- 不动 ethereum 并行 DMC（OP 独立串行类）。
- 不做 eth_call 的 `callAtBlock`（历史块查询）——YAGNI，现状 `call` 足够。

## 3. 关键决策（用户确认）

| 决策 | 定案 |
|---|---|
| **D1 eth_call 适配器** | **参照 op-alignment 的 `OpCallScheduler` 逻辑移植**（fork + loadOpFeeParams + blockGasLeft + chainId + RecentBlockHashes + executeTransaction 10 参注入），作为方案 B 组成部分重建 |
| **D2 commit/head 推进** | **并入**：`OpBlockScheduler::commitBlock` 含 `SYS_CURRENT_STATE` head 推进（一并解决 #19） |
| **D3 SFINAE probe** | **并入**：engine 的 `requires { &SchedulerType::executeOpBlock<...> }` 改为探测 `executeBlock` |
| **D4 编排状态** | blockGasLeft 递减在 `processOpBlock` 内，scheduler 只是调用方，**不动** |
| **D5 分类 barrier** | 留 **engine**（handleOpNewPayload）；OpSchedulerImpl 的 catch（processOpBlock throw → OpConsensusError/OpStorageError）移 scheduler |

## 4. 架构

```
engine newPayload → handleOpNewPayload（分类 barrier）→ OpBlockScheduler::executeBlock
                                                          → processOpBlock（块级编排，不动）
                                                          → 分类 catch → OpExecuteBlockResult（六项承诺）

RPC eth_call → MultiVersionScheduler v3 → OpBlockScheduler::call
                                              → fork + loadOpFeeParams + blockGasLeft + chainId + RecentBlockHashes
                                              → OpstackExecutor::executeTransaction（10 参注入）→ dry-run 丢弃

RPC/consensus commit → OpBlockScheduler::commitBlock
                          → 块落盘（registerOpBlock 语义）+ SYS_CURRENT_STATE head 推进
```

## 5. 组件设计

### 5.1 `OpBlockScheduler`（新建，opstack-executor/OpBlockScheduler.h）

`template <class Storage>`，实现 `bcos::scheduler::SchedulerInterface`。持有：`OpstackExecutor`（或 executeOpBlock 逻辑复用）+ `OpSchedulerImpl`（若保留为内部 helper）+ 注入参数（chainId/forkTimestamps）+ blockFactory + storage。

**方法**：
| 方法 | 行为 |
|---|---|
| `executeBlock(block, verify, cb)` | 封装 executeOpBlock 逻辑：decodeOneRawTx → processOpBlock → seal/stateRoot → 六项承诺；分类 catch（OpConsensusError → INVALID、OpStorageError → -32603） |
| `call(tx, cb)` | eth_call：fork 最新状态 → loadOpFeeParams + header.gasLimit + chainId + RecentBlockHashes → `OpstackExecutor::executeTransaction`（10 参注入）→ dry-run 丢弃；无效 → Error（JSON-RPC） |
| `commitBlock(header, cb)` | 块落盘（registerOpBlock 语义）+ `SYS_CURRENT_STATE` head 推进（#19） |
| `getCode`/`getABI`/`getPendingStorageAt` | 存储读（镜像 BaselineScheduler，executor 无关） |
| `preExecuteBlock`/`status`/`reset` | 显式拒绝 / no-op（块执行走 executeBlock，非 preExecuteBlock） |

### 5.2 engine 适配（engine/bcos-engine/EngineServiceImpl.h）
- SFINAE probe：`requires { &SchedulerType::executeOpBlock<...> }` → `&SchedulerType::executeBlock<...>`。
- `handleOpNewPayload` 的 OP 分支：`m_scheduler.get().executeOpBlock(...)` → `m_scheduler.get().executeBlock(block, ...)`。
- 分类 barrier 逻辑不变（OpConsensusError → INVALID、OpStorageError → -32603 在 barrier 内）。

### 5.3 装配（libinitializer/）
- MultiVersionScheduler slot 3：`OpCallScheduler` → `OpBlockScheduler<GlobalStateStorage::OpenedStorage>`。
- Initializer OP 分支：构建 `OpBlockScheduler`（复用 opChainId/forkTimestamps + OpstackExecutor/OpSchedulerImpl）。

### 5.4 去留
- `OpSchedulerImpl`：executeOpBlock 逻辑被 `OpBlockScheduler::executeBlock` 复用——**保留为内部 helper**（或逻辑迁入后废弃，二选一，实施时定）。
- `OpCallScheduler`：由 `OpBlockScheduler::call` 替代，**废弃**。

## 6. 数据流

### 6.1 块执行（newPayload）
```
engine newPayload(request, v4)
  → handleOpNewPayload（timestamp x version gate -38005）
  → runOpNewPayloadSteps → m_scheduler.executeBlock(block, ...)
    → decodeOneRawTx（raw 信封 → OpBlockTx）
    → processOpBlock（deposit 前置 / blockGasLeft 递减 / fee 加载 / 执行 / finalize）
    → sealOpBlock + stateRootOf → 六项承诺
  → barrier 分类：成功 → VALID + 六项比对；OpConsensusError → INVALID；OpStorageError → -32603
  → commitBlock（registerOpBlock + SYS_CURRENT_STATE head 推进）
```

### 6.2 eth_call
```
RPC eth_call → scheduler()->call(tx, cb) → OpBlockScheduler::call
  → task::wait → coCallLatest
    → view = storage.fork() + newMutable()
    → blockNumber/ledgerConfig/header（从 fork 取）
    → configAt(header.timestamp()/1000, forkTimestamps)
    → fee = loadOpFeeParams(view)（L1Block 槽，无 attributes 覆盖）
    → blockGasLeft = header.gasLimit()
    → RecentBlockHashes（种子 {N-1: parentHash}）
    → OpstackExecutor::executeTransaction(..., fee, blockGasLeft, chainId, &hashes)
    → 无效 → Error（JSON-RPC）；成功 → 回执（fork 丢弃）
```

## 7. 错误处理（分类）

| 层 | 分类 |
|---|---|
| **processOpBlock throw**（共识拒绝：首笔非 deposit、gas 超限、非法交易） | → `OpConsensusError`（移 scheduler 的 catch）→ **INVALID** |
| **Storage2Ledger 毒旗 / 存储故障** | → `OpStorageError` → **-32603**（非共识判定） |
| **engine barrier**（timestamp x version、静态校验） | 留 engine，不变 |
| **eth_call 无效调用** | → JSON-RPC **Error**（匹配 op-geth） |

## 8. 测试与验证

| 套件 | 验证 |
|---|---|
| 新增 `OpBlockSchedulerTest` | executeBlock（块执行 + 分类）、call（eth_call 注入 + Error 语义）、commitBlock（落盘 + head 推进）、存储读、拒绝语义 |
| 适配 `OpSchedulerImplSmokeTest`/`OpEngineBranchSmokeTest` | executeOpBlock 逻辑迁走后 |
| 适配 `OpCallSchedulerTest` | 重建的 eth_call 路径 |
| **等价重验** | t8n 77 + 引擎门 65 + 全套件全绿；enhanced-corpus 无效分类断言（INVALID/-32603）不回归 |

## 9. 风险与处置

| 风险 | 处置 |
|---|---|
| **共识路径改动**（块执行从 engine 直连迁 scheduler） | 等价重验（t8n/引擎门）为 gate；任何红先归因 |
| eth_call 适配器重建偏差 | 参照 op-alignment 的 OpCallScheduler + 单测（注入参数语义） |
| head 推进（#19）并入 | `SYS_CURRENT_STATE` 更新 + reorg 处理；单测覆盖 |
| 与并行统一工作（op-eth-executor-unification）交叉 | 独立 worktree，基于 executor-e2e 干净基址 |

## 10. 实施顺序（writing-plans 输入）

1. **`OpBlockScheduler` 骨架**：SchedulerInterface + executeBlock（复用 executeOpBlock 逻辑）
2. **engine 改调**：SFINAE probe + handleOpNewPayload
3. **eth_call 重建**：coCallLatest（参照 op-alignment）+ 存储读
4. **commitBlock + head 推进**：落盘 + SYS_CURRENT_STATE
5. **装配**：slot 3 + Initializer
6. **测试适配 + 等价重验**：smoke/call 适配 + t8n/引擎门/全套件
