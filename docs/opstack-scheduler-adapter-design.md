# OP scheduler 适配器设计(层次 2)

> 状态:2026-08-11 实施完成。#22(槽位+装配+通知)已交付,验证:OpstackExecutorTests 9/9、
> block-tests 全绿(t8n 33 向量 + OpCallSchedulerTest 2/2)、detail-tests 12/12;init/engine 编过。
> #23(块执行走 scheduler)经 §0 分析为不必要(op-geth 串行,processOpBlock 保留),不作实施。
> 前置分析:Explore agent 对 BaselineScheduler 驱动 executor 机制的深入核查(结论见 §0)。
> 本设计取代对齐方案文档中层次 2 的旧描述(原动机"获得并行/chunk 能力"被 §0 推翻)。

## §0 前置分析结论(agent 核查,决定设计方向)

1. **BaselineScheduler 无法承载 OP 块语义**:块路径逐交易驱动的是 concept 三阶段
   `createExecuteContext→prepare→execute→finish`,它从不调用 `executeTransaction`、从不调用
   executor 的 `finalizeBlock`。OP 的五条块级语义——deposit 前置、blockGasLeft 跨交易递减、
   fee 惰性加载、整块拒块(无失败回执)、块末 finalizeOpBlock——全部是**块级编排状态**,而
   concept 的 `ExecuteContext` 是每交易独立生命周期、无块级共享状态。把 OpstackExecutor 塞进
   BaselineScheduler 会在块路径静默跑出错误 OP 语义。
2. **op-geth 本来就是串行执行**:FISCO ethereum scheduler 的并行/chunk 是 FISCO 的优化,
   OP 对齐 op-geth 的语义就是串行。层次 2 原动机"获得并行/chunk"不成立,**processOpBlock 保留**。
3. **OP 模式下唯一语义分裂的 RPC 路径是 `call()`(eth_call)**:
   - `RPCServer::call`(RPCServer.cpp:57)走 `scheduler()->call()` → 当前版本 3 饱和到 ethereum
     scheduler → EthereumExecutor 用以太坊语义执行,不理解 OP predeploy/fee vault/deposit。
   - `getPendingStorageAt`/`getCode`/`getABI` 是纯存储读(TxValidator.cpp:205 的 txpool 校验也只
     走 `getPendingStorageAt`),与 executor 版本无关,任何 scheduler 读同一份已提交状态都对。
4. **eth_call 走 concept 6 参 `executeTransaction` 签名可行但默认注入不可用**:
   OpstackExecutor 的 6 参路径落到默认注入(fee={}, blockGasLeft=0, chainId=0),其中
   `blockGasLeft=0` 经 opValidate → evmone `validate_transaction` 会拒绝一切 gas_limit>0 的普通
   tx。必须走**带真实注入参数的 OP 专属 call 路径**。

## 目标

让 OP 模式的 RPC eth_call 走 OP 语义,并让 OP 调度器在调度体系中成为一等公民(版本 3 不再
静默饱和到 ethereum scheduler)。块执行路径(`EngineService→OpSchedulerImpl::executeOpBlock→
processOpBlock`)不动。

## 设计:OpCallScheduler 适配器 + 槽位 + 装配 + 通知

### 1. 新类 `OpCallScheduler`(opstack-executor/OpCallScheduler.h)

`template <class Storage>`(具体实例化 `GlobalStateStorage::OpenedStorage`),实现
`bcos::scheduler::SchedulerInterface`。内部持有一个 `OpstackExecutor` 实例 + 存储引用 + 注入
参数(chainId/forkTimestamps)+ blockFactory。

**构造**:`(receiptFactory, hashImpl, chainId, forkTimestamps, blockFactory, storage)`。

**`call()`(eth_call,核心)**,镜像 BaselineScheduler::coCallLatest 的取数序
(BaselineScheduler.h:914-926)+ OP 注入参数:

1. `view = storage.fork(); view.newMutable();`
2. `blockNumber = ledger::getCurrentBlockNumber(view, fromStorage)`
3. `ledgerConfig = ledger::getLedgerConfig(view, blockNumber, blockFactory)`
4. `block = ledger::getBlockData(view, blockNumber, HEADER, blockFactory)` → head header
5. `fee = op::loadOpFeeParams(view)`(读最新已提交 L1Block 各槽;无 attributes 覆盖,见 §风险)
6. `blockGasLeft = blockHeader.gasLimit`(tars 毫秒→秒交给 buildOpBlockInfo)
7. `chainId = ledgerConfig->chainId()`(链上权威,与注入值一致)
8. `blockHashes`:storage-backed 提供者(镜像 StorageBlockHashes 风格,BLOCKHASH 解析
   SYS_NUMBER_2_HASH)
9. 派发:tx 类型 0x7E → `executeDeposit`(需先 decode DepositTx);普通 → `executeTransaction`
   (全注入参数)
10. 返回回执(fork 丢弃,dry-run)

**其余 SchedulerInterface 方法**:

| 方法 | 行为 |
|---|---|
| `executeBlock` / `preExecuteBlock` / `commitBlock` | **抛**"OP 块执行走 engine(executeOpBlock),不经过 scheduler"——响亮失败,替代当前版本 3 静默饱和到 ethereum 的错路 |
| `getPendingStorageAt` / `getCode` / `getABI` | 存储读,镜像 ethereum scheduler 实现(executor 无关) |
| `status` / `reset` / `stop` | no-op 或最小实现(OP 无 pipeline 可停) |

### 2. MultiVersionScheduler 槽位

- `SUPPORTED_EXECUTOR_VERSION_COUNT` 3→4(MultiVersionScheduler.h:30)
- 数组 `{SchedulerManager, baseline, ethereum, op}`,构造函数签名 `array<..., 4>`
- `setVersion(3)` → index 3;>=4 饱和到 OP(更新饱和日志注释,currently 说 "newest 是 v2")
- 只影响版本 >=3 的 OP 节点;v1/v2 索引 0-2 不动

### 3. Initializer 装配(Initializer.{h,cpp})

- **修 ledger 漏传**(对齐方案问题 2):`EngineServiceInitializer::build(..., opScheduler,
  transactionExecutor, memPool, ledger)`——签名第 6 参已有默认值,补实参即可。
- OP 分支构建 `OpCallScheduler<GlobalStateStorage::OpenedStorage>`(复用 opChainId/forkTimestamps),
  加入 MultiVersionScheduler 数组 slot 3。
- 新增成员 `m_opSchedulerHolder` + `m_setOpSchedulerBlockNumberNotifier`(镜像
  m_ethereumSchedulerHolder 模式,Initializer.h:179-181)。

### 4. OP blockNumber 通知(对齐方案问题 3)

- EngineServiceImpl 加成员 `std::function<void(protocol::BlockNumber)>` + setter。
- OP VALID 分支 `mergeView`(EngineServiceImpl.h:1209)成功后触发,带 `payload.blockNumber`
  (块已提交才是"新块数")。
- Initializer::initNotificationHandlers 注入(镜像 L805-812 的 ethereum 接线)→
  `_rpc->asyncNotifyBlockNumber(groupID, nodeName, number, ...)`。

## 明确不做

- **并行/chunk**:op-geth 串行,processOpBlock 已正确(§0.2)。#23 收敛为"OP 块执行保持走
  executeOpBlock,不引入 scheduler pipeline",保留整块拒块语义。
- **失败回执**:破坏 op-geth 对拍(层次 3,已在路线图记录"不实施")。

## 风险与语义说明

1. **eth_call 的 DA footprint 标量**:块执行用首笔 attributes deposit calldata[176:178] 覆盖
   L1Block slot8;eth_call 无 attributes tx,`loadOpFeeParams(view)` 直接用最新已提交 slot8 值
   ——与块执行一致(dry-run 读最新状态,合理)。
2. **`call` flag 被忽略**(OpstackExecutor.h `(void)call`):eth_call 完全执行、diff 写进被
   丢弃的 fork——与 ethereum scheduler 同口径(dry-run on fork)。
3. **deposit 0x7E 的 eth_call**:RPC 正常不会来 deposit(web3 tx 无 0x7E 类型),v1 不做 deposit
   派发;若 0x7E 真到 call(),opValidate 按普通 tx 拒——响亮失败,可接受。
4. **实现期发现并修复的 OpstackExecutor 潜在分叉**(生产零引用时隐藏):
   - `buildOpBlockInfo` timestamp 原样(ms 而非 s)、且不读 prev_randao/parent_beacon_block_root/
     blob_gas_used/extra_data → 改为镜像 `toBlockInfo` 的字段集 + ms→s,可选字段容忍
     (value_or(0),保住最小 header 测试)。
   - `m_prepare`/`m_execute` base_fee 恒 0 → 改读 header.baseFee()(value_or(0))。effective
     gas price = base_fee + priority(OpTransition.cpp:266-271),恒 0 会让 eth_call 的 fee 与
     块执行分叉。
5. **验证**:t8n 33 向量 + e2e 63 用例全绿(块执行语义不变);OpCallScheduler 单测覆盖拒绝方法
   + eth_call 错误路径(空存储→callback Error)。全量 eth_call happy path(fee/baseFee/hashes
   注入对拍)需 seed ledger(evmcRevision/块表/L1Block),记为后续。

## 文件清单

- 新增:`opstack-executor/OpCallScheduler.h` + `opstack-executor/tests/OpCallSchedulerTest.cpp`
- 修改:`libinitializer/MultiVersionScheduler.{h,cpp}`
- 修改:`libinitializer/Initializer.{h,cpp}`
- 修改:`engine/bcos-engine/EngineServiceImpl.h`(notifier 成员 + setter + VALID 分支触发)
- 修改:`docs/opstack-alignment-plan.md`(层次 2 段落更新为适配器方案)

## 决策记录

- 2026-08-11:层次 2 采用**独立 OpCallScheduler 适配器**(非 BaselineScheduler 复用、非折叠进
  OpSchedulerImpl)——镜像 ethereum 双对象结构(engine 的 SchedulerParallelImpl 与 RPC 的
  BaselineScheduler 本就是两个对象),职责清晰。
