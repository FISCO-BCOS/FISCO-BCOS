# OP 驱动编排层对齐 — executeBlock/commitBlock 两阶段(设计)

> 日期:2026-08-11。分支 `worktree-op-alignment`(基于 feat-op-executor-e2e)。
> 状态:设计已批准(形态 B 两阶段;D1 方案③ 参考 ethereum 模式)。
> 前置:docs/op-block-exec-scheduler-unification-design.md(engine 调用面统一 + 共识比对下沉,已实施)。

## 背景与目标

OP 块执行由 EngineService 的 OP 分支直接驱动(`newPayload → executeOpBlock → registerOpBlock → mergeView`
一体),**不经 SchedulerInterface 的两阶段**(executeBlock/commitBlock)。而 ethereum(v2)的块执行
是标准两阶段:`SchedulerInterface::executeBlock`(执行)+ `commitBlock`(提交),驱动结构统一。

本设计把 OP 块执行对齐到 ethereum 的两阶段结构:
- **`executeBlock` 变真**:执行(processOpBlock)→ 返回 receipts + 暂存结果
- **`commitBlock` 变真**:落盘(写块表 + mergeView)+ notifier

对齐后,任何持有 `SchedulerInterface` 的组件都能以统一接口驱动 OP 块执行(与 ethereum 同构)。

**明确不做**:不重写 `processOpBlock` 的执行逻辑(执行语义零改动,对齐 op-geth 不变);不引入
并行/chunk(串行,已证伪)。

## ethereum 参考模式(BaselineScheduler.h:626-771)

```
coExecuteBlock: 执行 → 构建 Block + receipts → 存 m_results 队列(ExecuteResult)
coCommitBlock:
  1. 锁 + 连续性/重复检查
  2. 取 m_results.back()(executeBlock 暂存),校验 resultBlockNumber == header->number()
  3. 算 logsBloom → setBlockHeader/setLogsBloom
  4. ledger::prewriteBlockToBuffer(统一 prewrite → prewriteStorage)   [ledger 通用函数]
  5. m_multiLayerStorage.mergeBackStorage(prewriteStorage)             [一次 WriteBatch 原子落盘]
  6. ledgerConfig 更新 + notifier
```

三个关键模式:① executeBlock 暂存结果、commitBlock 取用(单对象两阶段);② 写块表是 **ledger
通用函数**(prewriteBlockToBuffer,非 scheduler 内嵌);③ prewrite + merge 原子落盘。

## 设计

### 1. OpSchedulerImpl 扩展(两阶段)

模板参数调整为 `<OpenedStorage>`(即 `GlobalStateStorage`),内部 `using ViewType =
OpenedStorage::ViewType`。构造新增注入(Initializer 组装,同 OpCallScheduler 模式):

```cpp
OpSchedulerImpl(receiptFactory, chainId, forkTimestamps,
    protocol::BlockFactory::Ptr blockFactory,      // 写块表(txHash 计算)
    OpenedStorage& storage)                         // commitBlock 落盘(mergeView)
```

新增成员/方法:

```cpp
struct PendingBlock {                 // executeBlock 暂存,commitBlock 取用(同 ethereum ExecuteResult)
    ViewType* view;                    // executeBlock 的 fork view(commitBlock 内 mergeView + 写块表)
    BlockHeader const* header;
    bcos::bytes rawTxBytes;
    OpExecuteBlockResult result;
};
std::optional<PendingBlock> m_pending;   // 单块串行,OP 模式安全

task::Task<vector<Receipt::Ptr>> executeBlock(   // 变真(concept 签名,兼容)
    OpenedStorage& storage, auto& /*executor*/, BlockHeader const& header,
    ::ranges::input_range auto const& rawTxBytes, LedgerConfig const& ledgerConfig)
{
    auto view = storage.fork();
    view.newMutable();
    auto result = co_await executeOpBlock(view, header, rawTxBytes);   // 复用现有执行
    m_pending = PendingBlock{&view, &header, rawTxBytes, std::move(result)};
    co_return m_pending->result.receipts;
}

OpExecuteBlockResult const& pendingExecuteResult() const { return m_pending->result; }
    // engine 6 项比对拿 commitments(ExecuteResult 含 seal/stateRoot/txRoot)

void commitBlock(BlockHeader::Ptr header, callback) override   // 变真
{
    // 1. 校验:取 m_pending,header->number() == m_pending->result 对应块号
    // 2. opstackRegisterBlock(*m_pending->view, *header, m_pending->rawTxBytes,
    //    m_pending->result, m_blockFactory)      [写 5 张表,原 registerOpBlock 逻辑下沉]
    // 3. m_storage.mergeView(std::move(*m_pending->view))   [原子落盘]
    // 4. notifyBlockNumber(header->number())
    // 5. 清空 m_pending;callback(nullptr, ledgerConfig)
    //    (ledgerConfig 从暂存:executeBlock 时传入的,或 commitBlock 内
    //     ledger::getLedgerConfig(*m_pending->view, header->number(), m_blockFactory) 读——实现选一,spec 定:executeBlock 暂存传入的 ledgerConfig,避免重复读)
```

**executeOpBlock 保留**:签名 `executeOpBlock(ViewType& view, BlockHeader const&, rawTxBytes) →
Task<OpExecuteBlockResult>` 不变,作为 `executeBlock` 的内部执行 + 公共入口(t8n/OpSchedulerImplTest
继续用它)。执行逻辑零改动。

### 2. opstackRegisterBlock 纯函数(写块表下沉,参考 prewriteBlockToBuffer)

从 engine 的 `registerOpBlock`(EngineServiceImpl.h:1233-1323)下沉为 opstack-executor 的通用函数:

```cpp
// opstack-executor/OpBlockRegister.h
// 写块表:等效 ethereum 的 ledger::prewriteBlockToBuffer。5 张表:
//   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
//   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
// 数据来源:header(blockNumber/blockHash=opHeaderHash)+ rawTxBytes + ExecuteResult(receipts/seal)
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    BlockHeader const& header, bcos::bytes const& rawTxBytes,
    OpExecuteBlockResult const& result, protocol::BlockFactory const& blockFactory);
```

- 逻辑逐行搬移自 engine `registerOpBlock`(写入判定、receipt 数量不变量、null receipt 检查、
  opEnvelopeToTars 转换、txHash = keccak(raw envelope) 全保留)。
- **opEnvelopeToTars 一并移到 opstack-executor**(OpEnvelopeToTars.h,从 engine/bcos-engine/
  EngineServiceImpl.cpp:33 搬移)。依赖 `bcos::rpc::Web3Transaction`(bcos-rpc model):**opstack-executor
  新增 link `rpc`**(一个权衡:opEnvelopeToTars 本应是通用 envelope→tars 转换,非 engine 专属)。

### 3. engine newPayload 三阶段(OP 分支瘦身)

`runOpNewPayloadSteps` 改为三阶段(替代现状的 executeOpBlock → 比对 → registerOpBlock + mergeView):

```cpp
// ---- 执行 ----
auto receipts = co_await m_scheduler.get().executeBlock(
    m_globalStateStorage.get(), m_executor.get(), *ethHeader,
    *payload.rawTransactions, *ledgerConfig);
// ---- 比对(pendingExecuteResult 拿 ExecuteResult → commitments) ----
const auto& executeResult = m_scheduler.get().pendingExecuteResult();
const auto commitments = SchedulerType::commitmentsOf(executeResult);
const auto announced = SchedulerType::announcedCommitmentsOf(payload, transactionsRoot, *ethHeader);
if (auto f = SchedulerType::mismatchedFieldOf(commitments, announced)) { ... INVALID ... }
// ---- 提交 ----
co_await m_scheduler.get().commitBlock(ethHeader);   // 内部写块表 + mergeView + notifier
co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
```

删除:engine 的 `registerOpBlock` 方法、`mergeView` 调用(落盘移入 commitBlock)、notifier 触发
(移入 commitBlock)。保留:版本 gate、静态验证、父链检查、比对段。

### 4. 错误处理(不变)

- `executeBlock` 内 executeOpBlock 的错误分类保留(ConsensusError → INVALID、StorageError → -32603)。
- `commitBlock` 落盘错误(写块表/mergeView 异常)→ `OpExecutionInternalError` → -32603(与现状
  registerOpBlock 的错误通道一致)。
- notifier 在 commitBlock 落盘成功后触发(对齐 ethereum commitBlock 语义)。

### 测试

1. **`opstackRegisterBlock` 单测**:构造 header + rawTxBytes + ExecuteResult + view,断言 5 张表
   写入正确(txHash/receipt 编码/tx 转换/块表)。
2. **`OpSchedulerImpl` 两阶段单测**:executeBlock(执行+暂存)→ pendingExecuteResult → commitBlock
   (落盘 + notifier),断言 view merge、m_pending 清空。
3. **engine newPayload 三阶段回归**:现有 OpNewPayloadRpcE2eTest(e2e 65 用例)覆盖执行+比对+提交
   全链路,全绿。
4. **t8n(127 向量)**:执行逻辑零改动(executeOpBlock 保留),全绿证明执行不变。

### 工作量与风险

- **工作量 ~1.5-2 周**。核心:registerOpBlock 下沉(数据来源改造 payload → header+result+rawTxBytes)、
  opEnvelopeToTars 共享(opstack-executor 加 rpc link)、OpSchedulerImpl 模板调整 + 依赖注入。
- **风险中高**:
  - registerOpBlock 数据来源改造(最易出错,需 e2e 全绿守块表写入正确)。
  - opstack-executor 新增 rpc 依赖(link 变大,模块纯度让步——参考 ethereum 的 prewrite 在 ledger 层)。
  - OpSchedulerImpl 模板参数调整(<ViewType> → <OpenedStorage>)影响 Initializer 实例化 + engine
    SchedulerType 依赖名(executeOpBlock 签名不变,依赖名兼容)。
  - **测试不可退步(强制)**:现有 t8n/e2e/detail-tests 全绿;新测试只加强。

### 明确不做

- ❌ 重写 processOpBlock 执行逻辑(对齐 op-geth 不变)。
- ❌ 并行/chunk(串行,已证伪)。
- ❌ 引入失败回执(破坏对拍)。

## 文件清单

| 文件 | 改动 |
|---|---|
| `opstack-executor/OpSchedulerImpl.h` | 模板 <OpenedStorage>;构造注入 blockFactory + storage;executeBlock 变真 + commitBlock 变真 + pendingExecuteResult + m_pending |
| `opstack-executor/OpBlockRegister.h` | **新增** opstackRegisterBlock 纯函数(从 engine registerOpBlock 下沉) |
| `opstack-executor/OpEnvelopeToTars.h/.cpp` | **新增**(从 engine 移入,依赖 Web3Transaction) |
| `opstack-executor/CMakeLists.txt` | link 加 `rpc` |
| `engine/bcos-engine/EngineServiceImpl.h` | newPayload 三阶段;删 registerOpBlock/mergeView/notifier; |
| `libinitializer/Initializer.cpp` | OpSchedulerImpl 构造传 blockFactory + storage |
| `opstack-executor/tests/` | opstackRegisterBlock 单测 + 两阶段单测(注册进现有 target) |

## 决策记录

- 2026-08-11:用户确认形态 B(两阶段 executeBlock/commitBlock),D1 方案③(写块表下沉为
  opstack-executor 纯函数 + opEnvelopeToTars 共享)——参考 ethereum 的 prewriteBlockToBuffer 在
  ledger 通用层。
