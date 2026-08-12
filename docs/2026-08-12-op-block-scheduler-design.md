# OpBlockScheduler — OP 调度门面（eth_call + slot 3 一等公民 + head 推进 #19）

> 状态：2026-08-12 修订 v2（3-agent 审查后收敛；用户选定「门面化 OpBlockScheduler」）。分支 `feat-op-block-scheduler`（基址 feat-op-executor-e2e `0ba5256e0`）。
> 前置：`docs/opstack-scheduler-adapter-design.md`（§0：processOpBlock 保留、BaselineScheduler 承载不了 OP 块语义、唯一 RPC 语义分裂是 call()）、`docs/op-eth-executor-unification-design.md`（执行层统一，processOpBlock 不动）、`docs/op-block-exec-scheduler-unification-design.md`（**已否决**块执行迁入 scheduler pipeline——「签名矛盾」，本设计尊重该否决）。

## 0. v1 → v2 变更（3-agent 审查结论）

v1「统一调度器」（块执行迁入 `SchedulerInterface::executeBlock`）经 3-agent 审查（事实 / 架构 / 风险完整性）被证伪，四项结构发现：

1. **`executeBlock` 签名载不动块执行**。`SchedulerInterface::executeBlock(block, verify, cb)`（SchedulerInterface.h:43-45）回调只回 `BlockHeader::Ptr`，带不回 `OpExecuteBlockResult`（六项承诺）；raw EIP-2718 信封（L1/DA fee 与 wire-byte txRoot 的输入）无法穿透 `protocol::Block::Ptr`（其 Transaction 仅有无 r/s/v 的签名前像，Transaction.h:89）。此即 `op-block-exec-scheduler-unification-design.md` 已否决的「签名矛盾」。
2. **单类不能兼 engine 与 RPC 两职**。engine 的 `SchedulerType` 须满足 `scheduler_v1::TransactionScheduler` concept（TransactionScheduler.h:11-20，要求 5 参 `executeBlock(storage, executor, header, txRange, ledgerConfig)` awaitable）；slot 3 须实现 `SchedulerInterface` 纯虚 `executeBlock(block, verify, cb)`（3 参）。同名冲突：3 参 override **遮蔽**继承的 5 参 concept 形 → concept 失败；双声明触发 `-Woverloaded-virtual`（-Werror，SchedulerInterface.h:59-66 注释明示此险，解法是「不同名」）。
3. **slot 3 前提错误**。当前基址 `MultiVersionScheduler` COUNT=3（MultiVersionScheduler.h:30），OP 模式**完全绕过** MVS（Initializer.cpp:454「untouched」，:482-487 直连 `OpSchedulerImpl`）。「4 槽 + slot3=OpCallScheduler」是 op-alignment 分支状态，非基址现状；slot 3 需从零搭（COUNT 3→4 + 饱和规则 + Initializer 接线）。
4. **`commitBlock` 载不动 registerOpBlock**。`commitBlock(header, cb)` 只收 header；OP registerOpBlock 需 view + payload + executeResult。scheduler 内部 stash 会打破 execute→verify→register→merge **单 view 原子流**。**#19 head 推进只能写进 engine `runOpNewPayloadSteps` 同 view**（merge 提交的那个 view）。

**v2 修正**：放弃「块执行经 `SchedulerInterface`」。方案 B 收缩为**门面化 OpBlockScheduler**——engine 块执行通道 100% 不变（SchedulerType=`OpSchedulerImpl` + `executeOpBlock` 依赖名，probe 不改名）；新增 `OpBlockScheduler` 作为 RPC slot-3 的 `SchedulerInterface` 面（call 真实现 + 存储读 + 拒绝 stub），继承 `OpSchedulerImpl` 复用 seam 与 executeOpBlock；#19 并入 engine 同 view。

## 1. 背景与问题

FISCO OP 模式当前两条执行路：

- **块执行**：engine newPayload 直连 `SchedulerType`（=`OpSchedulerImpl<ViewType>`）依赖名调用 `executeOpBlock`（→ processOpBlock），走六步：decode → Storage2State 桥 → processOpBlock → 分类 catch → seal/stateRoot → txRoot/gasUsed → `OpExecuteBlockResult`。
- **eth_call**：**当前基址没有 OP eth_call 路径**。RPC 的 `call()` 经 `MultiVersionScheduler.scheduler(version)` 路由，executor_version≥3 时饱和到 slot 2（ethereum executor）——用纯 Ethereum 语义跑 OP eth_call：无 L1Block fee、无 OP vault 路由、无 deposit。op-alignment 分支的 `OpCallScheduler` 修复了这一点但未合入本基址。

与 ethereum（`BaselineScheduler<EthereumExecutor>` 统一驱动块执行 + eth_call）不对称。方案 B 目标：OP 的 eth_call 走 OP 语义、OP 在调度体系成为一等公民（slot 3）、head 推进（#19）落地——**但块执行不迁入 SchedulerInterface**（结构上不可能，见 §0）。

## 2. 目标与范围

### 2.1 目标
- OP eth_call 走 OP 语义（L1Block fee + vault 路由），不再缺失 → `OpBlockScheduler::call`。
- OP 在 MultiVersionScheduler 成为 slot 3 一等公民（RPC 面路由到 OP 调度器）。
- #19 head 推进（`SYS_CURRENT_STATE`）落地（engine 同 view）。
- engine 块执行通道保持：SchedulerType=`OpSchedulerImpl`、`executeOpBlock` 依赖名、probe 不改名。

### 2.2 范围
1. 新建 `OpBlockScheduler`（opstack-executor/OpBlockScheduler.h）：`template <class ViewType, class OpenedStorage> class OpBlockScheduler : public OpSchedulerImpl<ViewType>, public bcos::scheduler::SchedulerInterface`。
2. MultiVersionScheduler 3→4（COUNT + 饱和 + Initializer OP 装配填充 slot 3）。
3. engine #19 head 推进（runOpNewPayloadSteps 同 view）。
4. eth_call 移植：参照 op-alignment 的 `OpCallScheduler` 逻辑，**必须含 `buildOpBlockInfo` 三处修复**（正确性依赖，见 §5.2）。
5. 等价重验：t8n 125 + 引擎门 65 + 全套件 + enhanced-corpus 分类断言（计数更正，见 §8）。

### 2.3 明确不做（YAGNI / 已证伪）
- **不迁块执行进 `SchedulerInterface::executeBlock`**（§0 证伪）。
- 不改 `executeOpBlock` 名 / SFINAE probe（D3）。
- 不迁 engine 分类 barrier（D5 两段式，scheduler 侧 catch 原样保留在 OpSchedulerImpl）。
- 不改 processOpBlock 块级编排 / blockGasLeft 递减（D4）。
- 不做 eth_call 的 `callAtBlock`（历史块查询）——YAGNI。
- 不做 `commitBlock` 迁移（registerOpBlock 留 engine 成员；OpBlockScheduler::commitBlock 为拒绝 stub）。

## 3. 关键决策（v2 修订后）

| 决策 | 定案 |
|---|---|
| **D1 eth_call** | 参照 op-alignment `OpCallScheduler` 移植（fork + loadOpFeeParams + blockGasLeft=header.gasLimit + chainId + RecentBlockHashes + `executeTransaction` 10 参注入）；**必须带 `buildOpBlockInfo` 三处修复**（ms→s、header.baseFee、全字段集） |
| **D2 #19 head 推进** | 并入 engine `runOpNewPayloadSteps` 同 view（registerOpBlock 同 view 写 `SYS_CURRENT_STATE`/`SYS_KEY_CURRENT_NUMBER`）；**不经** `SchedulerInterface::commitBlock` |
| **D3 SFINAE probe** | **不改名**：保持 `requires { &SchedulerType::template executeOpBlock<std::vector<bcos::bytes>>; }`（EngineServiceImpl.h:205-206） |
| **D4 编排状态** | blockGasLeft 递减在 `processOpBlock` 内，不动 |
| **D5 分类 barrier** | 两段式：OpSchedulerImpl 的 typed-catch（含 RTTI bypass）**原样保留**；engine barrier 只做 typed → INVALID/-32603 映射 |
| **D6 形态** | **门面化**：OpBlockScheduler 公开继承 OpSchedulerImpl（免费获得 seam 9 名 + executeOpBlock + RTTI catch）+ 实现 SchedulerInterface（call 真、executeBlock/commitBlock/preExecuteBlock 拒绝、存储读、status/reset no-op）。engine SchedulerType 保持 `OpSchedulerImpl`（结构锁定，见 §0-2） |
| **D7 装配** | MultiVersionScheduler COUNT 3→4、饱和 `>=3 → 3`（自动，加槽即生效）、slot 3 = `OpBlockScheduler<ViewType, OpenedStorage>`；OP 模式 Initializer 接线 |

## 4. 架构

```
engine newPayload → handleOpNewPayload（分类 barrier，不变）→ SchedulerType(=OpSchedulerImpl)::executeOpBlock  [通道不变]
                                                              → processOpBlock（块级编排，不动）
                                                              → 分类 catch（typed + RTTI bypass，原样）→ OpExecuteBlockResult
                                                              → registerOpBlock（engine 成员）+ [NEW] 同 view 写 head (#19)

RPC eth_call → MultiVersionScheduler.scheduler(executor_version)  → v>=3 → slot 3 = OpBlockScheduler::call
              → fork OpenedStorage → ViewType → buildOpBlockInfo(三处修复) → loadOpFeeParams(L1Block 槽)
              → blockGasLeft=header.gasLimit → chainId → RecentBlockHashes(seed {N-1: parentHash})
              → OpstackExecutor::executeTransaction(view, header, tx, 0, ledgerConfig, call=true, fee, blockGasLeft, chainId, &hashes)
              → 无效 → Error(JSON-RPC)；成功 → 回执（fork 丢弃，dry-run）

RPC 存储读 getCode/getABI/getPendingStorageAt → OpBlockScheduler（镜像 BaselineScheduler，executor 无关）
```

## 5. 组件设计

### 5.1 `OpBlockScheduler`（新建，opstack-executor/OpBlockScheduler.h）

```cpp
template <class ViewType, class OpenedStorage>
class OpBlockScheduler : public OpSchedulerImpl<ViewType>,
                         public bcos::scheduler::SchedulerInterface
```

- **继承 `OpSchedulerImpl<ViewType>`**：免费获得 seam 9 名（`BlockEnv`/`ExecuteResult`/`ConsensusError`/`StorageError`/`c_ethRawTxTable`/`commitmentsOf`/`computeTxRoot`/`isIsthmusActiveAt`/`isJovianActiveAt`，OpSchedulerImpl.h:109-159）+ `executeOpBlock` + RTTI 双 catch（:240-273）+ 概念形 dummy `executeBlock`（被本类 3 参 override 遮蔽——无害：engine 不用本类作 SchedulerType，concept 检查不针对本类）。engine 未来如需经此类路由块执行，机制已备。
- **SchedulerInterface 面**：
  | 方法 | 行为 |
  |---|---|
  | `call(tx, cb)` | **OP eth_call**（真实现）：fork OpenedStorage → ViewType → `getCurrentBlockNumber`/`getLedgerConfig`/`getBlockData` → `configAt(header.timestamp()/1000, forkTimestamps)` → `buildOpBlockInfo`（三处修复）→ `loadOpFeeParams` → blockGasLeft = `narrowU256ToU64(header.gasLimit())` → `RecentBlockHashes`（seed {N-1: parentHash}）→ `OpstackExecutor::executeTransaction(view, header, *tx, 0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, chainId, &hashes)` → 无效 → Error（JSON-RPC）；成功 → 回执（fork 丢弃）。镜像 OpCallScheduler.h:247-299 + 修复 |
  | `executeBlock` / `commitBlock` / `preExecuteBlock` | **响亮拒绝** stub（照抄 OpCallScheduler.h:194-218：块执行 engine 驱动、commit engine 驱动） |
  | `getCode` / `getABI` / `getPendingStorageAt` | 存储读（镜像 BaselineScheduler.h:1018-1076 / OpCallScheduler.h:114-190，executor 无关） |
  | `status` / `reset` | no-op（OpCallScheduler.h:222-228） |
- **构造**：receiptFactory, hashImpl, chainId, forkTimestamps, blockFactory, `OpenedStorage&`（组合根注入）。
- **类型参数**：`ViewType` = engine 传入的 fork view（`OpSchedulerImpl` 基类 seam/executeOpBlock 的 Storage 参数）；`OpenedStorage` = 可 fork 的 MLS（RPC call() 每调用 fork）。实例化 `OpBlockScheduler<GlobalStateStorage::ViewType, GlobalStateStorage>`。

### 5.2 `buildOpBlockInfo` 三处修复（eth_call 正确性依赖）

`OpstackExecutor::buildOpBlockInfo`（OpstackExecutor.h:78-90）现为：`timestamp` 原样（ms）、`base_fee` 由调用点注入且恒传 0（:258/:289）、仅 number/timestamp/gas_limit/base_fee/coinbase（prev_randao/beacon_root/extra_data/blob_gas_used 恒零）。该函数**只服务 eth_call**（块路径用 `toBlockInfo`，OpRlpDecode.h:112，`/1000` 语义）——修复无块路径回归。对照 op-alignment 的 OpstackExecutor（:275-277/:309-311）：

1. `blk.timestamp = header.timestamp() / 1000`（ms → s，匹配块路径 `toBlockInfo`，`configAt` 消费秒）；
2. `base_fee` 取自 `header.baseFee().value_or(0)`（不再由调用点恒传 0）；
3. 全字段集：`prev_randao`/`beacon_root`/`extra_data`/`blob_gas_used` 从 header 填（当前恒零）。

### 5.3 engine #19 head 推进（engine/bcos-engine/EngineServiceImpl.h）

`runOpNewPayloadSteps` 内、registerOpBlock（:1233 成员，调用点 :1204）**同 view**（merge 提交的 view）写入：
- `SYS_CURRENT_STATE` → head blockNumber；
- `SYS_KEY_CURRENT_NUMBER` → head number。

现缺口在 :1206-1208（「SYS_CURRENT_STATE head advance is still missing… deferred to the orchestration layer」）。单调推进（父 == 当前 tip 才推进；reorg-window 编排 #19 注释明示 deferred，本设计不引入 reorg 支持）。

### 5.4 MultiVersionScheduler 3→4 + 装配（libinitializer/）

- `MultiVersionScheduler.h:30`：`SUPPORTED_EXECUTOR_VERSION_COUNT` 3→4。
- `MultiVersionScheduler.cpp`：饱和规则现为 `min(version, size-1)`（加槽后自动变 `>=3 → 3`）；:96-105 的 WARNING log 注释同步更新（"newest" = slot 3）。`scheduler(version)` 是 `.at()` 直接索引（MultiVersionScheduler.cpp:107-111）——数组有第 4 槽后 `scheduler(3)` 即返回 OpBlockScheduler。
- `Initializer.cpp` OP 分支（:456-497）：engine 构建 `OpSchedulerImpl<ViewType>` **保持不变**（含 :492-496 static_assert c_opMode）；新增 `make_shared<OpBlockScheduler<GlobalStateStorage::ViewType, GlobalStateStorage>>` 填入 MVS 数组第 4 槽（:509-513）。装配时确认 RPC 侧 `scheduler(executor_version)` 路由（executor_version≥3 → slot 3；op-alignment 的 notifier 接线模式可参照）。

### 5.5 去留
- `OpSchedulerImpl`：**保留**（engine SchedulerType + seam 载体 + executeOpBlock 宿主 + RTTI catch）。D5「catch 移 scheduler」不成立——catch 本就在 OpSchedulerImpl，原样保留。
- `OpCallScheduler`（op-alignment 分支）：不迁移；本 worktree 新建 `OpBlockScheduler` 吸收其 call/存储读逻辑（继承 OpSchedulerImpl 的演进版）。

## 6. 数据流

### 6.1 块执行（newPayload，通道不变 + #19）
```
engine newPayload → handleOpNewPayload（timestamp x version gate -38005）
  → runOpNewPayloadSteps → SchedulerType::executeOpBlock(view, *ethHeader, rawTransactions)
    → decodeOneRawTx → Storage2State 桥 → processOpBlock（deposit 前置/blockGasLeft/fee/执行/finalize）
    → 分类 catch → sealOpBlock + stateRootOf → txRoot + gasUsed → OpExecuteBlockResult
  → barrier 分类：成功 → VALID + 六项比对；OpConsensusError → INVALID；OpStorageError → -32603
  → registerOpBlock（engine 成员）+ [NEW] 同 view 写 SYS_CURRENT_STATE / SYS_KEY_CURRENT_NUMBER（#19）
```

### 6.2 eth_call（新路径）
```
RPC eth_call → scheduler()->call(tx, cb) → slot 3 OpBlockScheduler::call
  → task::wait → coCallLatest
    → view = storage.fork() + newMutable()
    → blockNumber/ledgerConfig/header（从 fork 取）
    → configAt(header.timestamp()/1000, forkTimestamps)
    → buildOpBlockInfo(header)（三处修复：ms→s / header.baseFee / 全字段）
    → fee = loadOpFeeParams(view)（L1Block 槽，无 attributes 覆盖）
    → blockGasLeft = narrowU256ToU64(header.gasLimit())
    → RecentBlockHashes（seed {N-1: parentHash}）
    → OpstackExecutor::executeTransaction(view, header, *tx, 0, *ledgerConfig, call=true, fee, blockGasLeft, chainId, &hashes)
    → 无效 → Error(JSON-RPC)；成功 → 回执（fork 丢弃）
```

## 7. 错误处理（分类）

| 层 | 分类 |
|---|---|
| **processOpBlock throw**（共识拒绝：首笔非 deposit、gas 超限、非法交易） | OpSchedulerImpl typed-catch RTTI bypass → `OpConsensusError` → engine barrier → **INVALID** |
| **Storage2Ledger 毒旗 / 存储故障** | `OpStorageError`（poisoned()-first，:251-275 原样）→ **-32603** |
| **RTTI 双 catch**（-fno-rtti libevmone 边界） | 原样保留（OpSchedulerImpl.h:240-273）；搬错会静默把共识拒绝误判 -32603 |
| **engine barrier**（timestamp x version、静态校验） | 留 engine，不变 |
| **eth_call 无效调用** | → JSON-RPC **Error**（匹配 op-geth；OpstackExecutor 抛 `OpTxValidationFailed` 等被 call() catch → `BCOS_ERROR_PTR`） |

## 8. 测试与验证

| 套件 | 验证 | 计数 |
|---|---|---|
| 新增 `OpBlockSchedulerTest` | call：**happy path 注入对拍**（fee/baseFee/hashes 参数语义；fixture 显式 seed `SYS_CURRENT_STATE` + L1Block 槽 + evmcRevision——head 未推进时 `getCurrentBlockNumber` 读 genesis）；Error 语义（无效调用 → JSON-RPC Error）；拒绝 stub（executeBlock/commitBlock/preExecuteBlock）；存储读 | — |
| #19 断言 | registerOpBlock 后 `SYS_CURRENT_STATE`/`SYS_KEY_CURRENT_NUMBER` 单调推进（**reorg 不测**，当前无 reorg 支持） | — |
| 适配 `OpSchedulerImplSmokeTest` / `OpEngineBranchSmokeTest` | executeOpBlock 逻辑不动，回归保护 | — |
| 等价重验 | t8n 重放 + 引擎门 + 全套件全绿 | t8n **125**（manifest 实际登记：77 Phase-2 + 48 enhanced）；引擎门 **65**（63 single + 2 chained，OpNewPayloadRpcE2eTest.cpp:1015-1017） |
| enhanced-corpus 分类断言 | invalid-tx 18 / static 10 / corrupt 12 / chain 6 / legacy 2 的 INVALID / -32603 / -38005 分类不回归（OpNewPayloadRpcE2eTest runner :380-540） | pin 用例清单 |

**计数更正**（v1 写 t8n 77 低估）：t8n manifest 实为 125、engine golden 实为 63 single（65 = 63+2 chained）。验收**不写死数字**，以 manifest/golden 文件集驱动（`OpT8nReplayTest.cpp:1268-1294` 断言目录集==manifest 全集）。

## 9. 风险与处置

| 风险 | 处置 |
|---|---|
| 与 op-eth-executor-unification 交叉（将重写 processOpBlock/Storage2State/OpstackExecutor） | 声明 **OpBlockScheduler 公共接口契约稳定**（executeBlock/call/commitBlock 签名 + seam surface），内部实现允许替换；独立 worktree 干净基址 |
| eth_call 与 unification 对 OpstackExecutor 双向依赖 | eth_call 语义锁定点：buildOpBlockInfo 修复 + 10 参注入契约固化进单测 |
| buildOpBlockInfo 修复波及块路径 | 无回归——该函数只服务 eth_call（块路径用 toBlockInfo）；修复后跑 t8n 门确认 |
| RTTI 双 catch 搬错 | 原样保留在 OpSchedulerImpl（不搬）；分类断言覆盖 |
| eth_call happy path 假覆盖 | fixture 显式 seed ledger（SYS_CURRENT_STATE + L1Block + evmcRevision），happy path 注入对拍非仅 Error 路径 |
| VM 实例数 / 串行化 | OpSchedulerImpl 持 1 VM（块）；OpBlockScheduler::call 每调用建 OpstackExecutor（各带 VM，RPC dry-run 可接受，同 OpCallScheduler 注释）；x_state 串行化假设仅块路径，RPC 路径不依赖 |
| slot 3 饱和语义回归（v≥3 原饱和 slot 2） | 加槽后自动 `>=3 → 3`；MVS 测试确认 `scheduler(3)` 返回 OpBlockScheduler 而非 ethereum |
| 装配漏 notifier / RPC 路由 | 参照 op-alignment 接线；`scheduler(executor_version)` 路由用单测锁死 |

## 10. 实施顺序（writing-plans 输入）

1. **`OpBlockScheduler` 骨架**：继承 OpSchedulerImpl + SchedulerInterface 面（call 先 stub、存储读、拒绝 stub、status/reset）。
2. **MultiVersionScheduler 3→4 + 装配 slot 3**：COUNT + MVS 测试（`scheduler(3)` 路由）+ Initializer OP 分支接线。
3. **engine #19 head 推进**：runOpNewPayloadSteps 同 view 写 `SYS_CURRENT_STATE`/`SYS_KEY_CURRENT_NUMBER` + 单调推进单测。
4. **`buildOpBlockInfo` 三处修复 + coCallLatest 移植**（call 真实现；happy path 依赖 #19 落地）。
5. **测试适配 + 等价重验**：OpBlockSchedulerTest（call happy/Error/拒绝/存储读 + #19 断言）+ smoke 适配 + t8n 125/引擎门 65/全套件 + enhanced-corpus 分类断言。
