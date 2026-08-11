# OP 驱动编排层对齐 — executeBlock/commitBlock 两阶段(设计)

> 日期:2026-08-11。分支 `worktree-op-alignment`(基于 feat-op-executor-e2e)。
> 状态:形态 B(两阶段)与 D1 方案③(写块表下沉)已批准;2026-08-11 四维 sub-agent 审查
> (技术正确性/架构一致性/完整性与测试/实现可行性)后修订,**本版合并全部审查意见**,可进入 writing-plans。
> 前置:docs/op-block-exec-scheduler-unification-design.md(engine 调用面统一 + 共识比对下沉,已实施)。

## 背景与目标

OP 块执行由 EngineService 的 OP 分支直接驱动(`newPayload → executeOpBlock → registerOpBlock → mergeView`
一体),**不经两阶段**(executeBlock/commitBlock)。而 ethereum(v2)的块执行是标准两阶段:
`SchedulerInterface::executeBlock`(执行)+ `commitBlock`(提交)。

本设计把 OP 块执行对齐到 ethereum 的两阶段结构:
- **`executeBlock` 变真**:执行(processOpBlock)→ 返回 receipts + 暂存结果
- **`commitBlock` 变真**:落盘(写块表 + mergeView)+ notifier

**驱动的诚实表述**(审查修正):engine 经 `SchedulerType` 模板 seam 触达 OpSchedulerImpl,而**不是**
SchedulerInterface(MultiVersionScheduler slot 3 是 RPC 用的 OpCallScheduler,块执行不经它)。所以
两阶段拆分的价值是**驱动结构对齐 + commit 收敛为单一落盘点**,不是"任何 SchedulerInterface 持有者
都能驱动"。engine 的 newPayload 现有守卫(父链检查、已知块去重、occupied-height)即 OP 的
**commit gate**,保留在 engine;`commitBlock` 是仅对 engine 开放的 seam 方法(见 §3 角色)。

**明确不做**:不重写 `processOpBlock` 的执行逻辑(执行语义零改动,对齐 op-geth 不变);不引入
并行/chunk(串行,已证伪);不做 SchedulerInterface 化(OpSchedulerImpl 不实现 SchedulerInterface)。

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

三个关键模式:① executeBlock 暂存结果、commitBlock 取用(单对象两阶段);② 写块表是通用函数
(ethereum 在 ledger 层,本设计下沉到 opstack-executor 纯函数,见 §2);③ prewrite + merge 原子落盘。

## 设计

### 1. OpSchedulerImpl 扩展(两阶段)— 审查修订

**模板参数(审查必改)**:保持首参 `Storage`(= `ViewType`,被 engine requires 子句钉死,改掉会击穿
`TransactionScheduler` 概念,见审查 CRITICAL-1),**新增第二模板参 `OpenedStorage = Storage`** 承载
落盘目标(同 OpCallScheduler `template <class GlobalStateStorage>` 持有 `Storage&` 的模式):

```cpp
template <class Storage, class OpenedStorage = Storage>   // Storage == ViewType(概念钉死)
class OpSchedulerImpl
```

构造新增注入(Initializer 组装;OpenedStorage=Storage 的默认仅用于只执行不 commit 的测试):

```cpp
OpSchedulerImpl(receiptFactory, chainId, forkTimestamps,
    protocol::BlockFactory::Ptr blockFactory,      // 写块表(txHash 计算)
    OpenedStorage& storage,                        // commitBlock 落盘(mergeView)
    EnvelopeToTarsConverter envelopeToTars)        // raw envelope → tars Transaction(见 §2,注入回调)
```

**新增成员/方法**:

```cpp
struct PendingBlock {
    ViewType view;                    // 按值 move 进来,持有所有权(审查必改:不得存指针,
                                      // executeBlock 协程局部 co_return 后即悬垂,UAF)
    std::vector<bcos::bytes> rawTxBytes;  // 物化的 envelope 列表(不是单字节串)
    OpExecuteBlockResult result;
    bcos::protocol::BlockNumber blockNumber;  // commitBlock 校验用(OpExecuteBlockResult 无块号)
};
std::optional<PendingBlock> m_pending;   // 单槽,OP 串行安全(engine x_state 串行化是不变量,见 §4)

task::Task<std::vector<TransactionReceipt::Ptr>> executeBlock(   // 变真,concept 5 参签名
    Storage& view, auto& /*executor*/, BlockHeader const& header,
    ::ranges::input_range auto const& rawTxBytes, LedgerConfig const& /*ledgerConfig*/)
{
    auto result = co_await executeOpBlock(view, header, rawTxBytes);   // 复用现有执行,零改动
    m_pending = PendingBlock{std::move(view), materialize(rawTxBytes),
        std::move(result), header.number()};
    co_return m_pending->result.receipts;
}
// 契约:executeBlock 从参数 move view 进 pending → 所有权转移;engine 在 executeBlock 返回后
// 不得再触碰该 view(比对走 pendingExecuteResult,commitBlock 用 m_pending->view,engine 不再需要)。

OpExecuteBlockResult const& pendingExecuteResult() const   // engine 比对拿 commitments
{
    if (!m_pending)
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
            "pendingExecuteResult: no pending executeBlock result"});
    return m_pending->result;
}

void resetPending() { m_pending.reset(); }   // 比对 INVALID 分支调用,避免残留带 mutable 层的视图

task::Task<void> commitBlock(   // seam 方法,不是 SchedulerInterface override(审查必改)
    BlockHeader::Ptr header, bcos::crypto::HashType const& blockHash)
{
    if (!m_pending) { /* 抛 OpExecutionInternalError:无待提交结果 */ }
    if (header->number() != m_pending->blockNumber) { /* 抛 OpExecutionInternalError:块号不符 */ }
    co_await opstackRegisterBlock(m_pending->view, *header, blockHash, m_pending->rawTxBytes,
        m_pending->result, *m_blockFactory, m_envelopeToTars);   // 写 5 张表(§2)
    co_await m_storage.mergeView(std::move(m_pending->view));    // 一次 WriteBatch 原子落盘
    m_pending.reset();
    notifyBlockNumber(header->number());                         // notifier 落盘成功后触发
}
```

**executeOpBlock 保留**:签名 `executeOpBlock(ViewType& view, BlockHeader const&, rawTxBytes) →
Task<OpExecuteBlockResult>` 不变,作为 executeBlock 的内部执行 + 公共入口(t8n/OpSchedulerImplSmokeTest
继续用它)。执行逻辑零改动。**双 fork 消除**(审查修订):engine 沿用现状的一次 fork + newMutable,
把 view 传入 executeBlock(不再有"预检查 fork + 执行 fork"两个实例)。

### 2. opstackRegisterBlock 纯函数 + 转换器注入(写块表下沉,审查修订)

从 engine 的 `registerOpBlock`(EngineServiceImpl.h:1204-1323)下沉为 opstack-executor 的通用函数,
等效 ethereum 的 ledger 通用写表(本设计放在 opstack-executor,不在 ledger 层——保持与已交付的
seam/纯函数体系一致):

```cpp
// opstack-executor/OpBlockRegister.h
// 写块表:等效 ethereum 的 ledger::prewriteBlockToBuffer。5 张表:
//   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
//   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;   // 签名与 opEnvelopeToTars 一致

template <class ViewType>
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash,
    std::vector<bcos::bytes> const& rawTxBytes, bcos::evm::engine::OpExecuteBlockResult const& result,
    bcos::protocol::BlockFactory& blockFactory, EnvelopeToTarsConverter const& envelopeToTars);
```

- 逻辑逐行搬移自 engine `registerOpBlock`(写入判定、receipt 数量不变量、null receipt 检查、
  txHash = keccak(raw envelope) 全保留)。数据来源映射:
  `payload.blockNumber → header.number()`、`payload.blockHash → blockHash 参数`(审查必改:
  引擎 step 2 已校验 `ethHeader->opHeaderHash(detail::opHeaderConst()) == payload.blockHash`,
  **显式传值,不搬 opHeaderConst** 进 opstack-executor,避免常量三处漂移)、
  `*payload.rawTransactions → rawTxBytes 参数`、`m_blockFactory->cryptoSuite()->hashImpl() →
  blockFactory`。
- **转换器注入,不搬 opEnvelopeToTars**(审查修订,替换原 D1 ③ 的"移到 opstack-executor + link rpc"):
  - `opEnvelopeToTars` 是 wire-format 转换(依赖 `bcos::rpc::Web3Transaction`),**留在 engine**
    (EngineServiceImpl.cpp:33),由 composition root(Initializer,已链 engine+rpc)以 lambda 注入:
    `[](bcos::bytes const& env, bcos::crypto::HashType const& h) {
       return bcos::engine::detail::opEnvelopeToTars(env, h); }`
  - **opstack-executor 保持 rpc-free**(不用背 jsoncpp/jwt/tars-servant/boostssl);无
    install(EXPORT) 隐患;单测用假转换器,opstackRegisterBlock 纯函数测试更干净。
  - `bcostars::Transaction` 经 bcos-ledger(LedgerMethods.h,opstack-executor 已 link)可见,
    无新增依赖。
  - 附带:`OpEnvelopeToTarsTest`(bcos-evm/test/opstack/OpEnvelopeToTarsTest.cpp)不受影响,
    不需迁移。

### 3. engine newPayload 三阶段(OP 分支瘦身)

`runOpNewPayloadSteps` 改为三阶段。**engine 沿用现状的一次 fork + newMutable**(父链检查同 view),
另在 executeBlock 前获取 ledgerConfig(OP 路径现状无此对象,审查必改):

```cpp
// ---- 前置(现状保留):版本 gate / 静态 blockHash 验证 / 父链检查(同一 view) ----
auto view = ...; view.newMutable();
auto ledgerConfig = ledger::getLedgerConfig(view, ethHeader->number(), m_blockFactory);
//   (OP 现状不消费 ledgerConfig,仅供 concept 合规;获取路径沿用 buildPayload 的
//    ledger::getLedgerConfig 模式,EngineServiceImpl.h:1377-1378)

// ---- 执行(保留现有 try/catch 错误分类;executeOpBlock 内部 typed-catch 不变) ----
std::vector<TransactionReceipt::Ptr> receipts;
try
{
    receipts = co_await m_scheduler.get().executeBlock(
        view, m_executor.get(), *ethHeader, *payload.rawTransactions, ledgerConfig);
    // 注意:executeBlock 已把 view move 进 pending,此后 engine 不得再触碰 view
}
catch (const SchedulerType::ConsensusError&) { co_return /* INVALID */; }
catch (const SchedulerType::StorageError&) { co_return /* -32603 */; }
catch (...) { co_return /* -32603 */; }

// ---- 比对(pendingExecuteResult 拿 ExecuteResult → commitments) ----
const auto& executeResult = m_scheduler.get().pendingExecuteResult();
const auto commitments = SchedulerType::commitmentsOf(executeResult);
const auto announced = SchedulerType::announcedCommitmentsOf(payload, transactionsRoot, *ethHeader);
if (auto f = SchedulerType::mismatchedFieldOf(commitments, announced))
{
    m_scheduler.get().resetPending();      // 审查补:INVALID 分支清残留
    co_return makeStatus(Invalid, parentHash,
        std::string("execution result does not match payload field: ") + *f);
}

// ---- 提交 ----
co_await m_scheduler.get().commitBlock(ethHeader, payload.blockHash);   // 内部写表+merge+notifier
co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
```

删除:engine 的 `registerOpBlock` 方法(EngineServiceImpl.h:1204-1323)、`mergeView` 调用
(落盘移入 commitBlock)、notifyBlockNumber 调用(移入 commitBlock)。保留:版本 gate、静态验证、
父链检查、比对段、try/catch 错误分类屏障。`opEnvelopeToTars`(cpp:33 + h:160-169 前向声明)
**保留**(被 Initializer 的转换器 lambda 引用)。

### 4. 错误处理 + 并发契约(不变 + 审查补充)

- `executeBlock` 内 executeOpBlock 的错误分类保留(ConsensusError → INVALID、StorageError → -32603,
  engine 侧 try/catch 在新调用点原样保留)。
- `commitBlock` 落盘错误(写块表/mergeView/空 pending/块号不符 → OpExecutionInternalError)→ 经
  handleOpNewPayload 屏障(catch OpExecutionInternalError rethrow → 其余 catch(...) → -32603),
  与现状 registerOpBlock 的错误通道一致。
- notifier 在 commitBlock 落盘成功后触发(对齐 ethereum commitBlock 语义);比对 INVALID 与已知块
  短路均不触发(短路在 commitBlock 之前 return,与现状等价)。
- **并发契约(成文)**:OP 模式下 engine 的 `x_state` 串行化保证同一条链同一时刻至多一个
  execute/commit 在途,单槽 `m_pending` 无锁安全。`commitBlock` 仅由 engine 经 SchedulerType
  seam 调用,不做通用并发声明(与 ethereum 的 m_commitMutex 队列相比,这是串行化特化)。

### 测试

1. **`opstackRegisterBlock` 单测**(纯函数,注入假转换器;归属 `opstack-executor-block-tests`):
   - happy:5 张表写入(txHash=keccak(raw envelope)、receipt 编码、tars tx 转换、header tars 编码);
   - 负例:receipt 数量不匹配 → OpExecutionInternalError;null receipt → OpExecutionInternalError;
     writeOne 抛错 → 异常上抛;非法 envelope → 行跳过、块仍有效(SYS_HASH_2_TX 缺行)。
2. **`OpSchedulerImpl` 两阶段单测**(归属 `opstack-executor-block-tests`):
   - happy:executeBlock → pendingExecuteResult → commitBlock,断言 view merge、m_pending 清空、
     notifier 恰好一次;
   - 负例:commitBlock 空 m_pending → 错误;commitBlock 块号不符 → 错误;
   - 链式:commit 后 execute 新块正常;比对 INVALID → resetPending 后 execute 新块正常(自愈)。
3. **engine newPayload 三阶段回归**:`OpNewPayloadRpcE2eTest`(**74 例 newPayload + 6 例 forkchoice
   = 80 例**,设计原写 65 不准)覆盖执行+比对+提交全链路,全绿;比对 INVALID、已知块短路路径不触发
   notifier。
4. **t8n(127 向量)**:执行逻辑零改动(executeOpBlock 保留),全绿证明执行不变。注意 t8n 直驱
   processOpBlock/executeOpBlock,**不经过两阶段编排**,不是编排重构的回归门;真正回归门是 e2e 80 例。
5. **测试名修正**:`OpSchedulerImplSmokeTest.cpp`(不是 OpSchedulerImplTest)。受模板/构造影响的
   实例化点全部更新:`Initializer.cpp:486`、`OpNewPayloadRpcE2eTest.cpp:140`、
   `OpSchedulerImplSmokeTest.cpp:106/151`、`OpEngineBranchSmokeTest.cpp:95/110`、
   `OpL1EdgeGateTest.cpp:164`(构造参数 3→6,commit 测试用带 mergeView 的存储实例化第二模板参)。

### 工作量与风险

- **工作量 ~1.5-2 周**(审查确认:CRITICAL-1/2 修正后现实)。核心:registerOpBlock 下沉
  (数据来源 payload → header+blockHash+rawTxBytes)、转换器注入(替代 rpc link,工作量下降)、
  OpSchedulerImpl 构造扩展 + 三阶段改写。
- **风险中低**(审查后下调):registerOpBlock 数据来源改造(最易出错,需 e2e 全绿守块表写入正确,
  先纯函数单测钉死);模板第二参 + 构造参数变化(5 处实例化点,编译闸);转换器注入链路
  (Initializer 组装,opEnvelopeToTars 仍驻 engine,零迁移)。
- **前向依赖注记**:概念形式 executeBlock 也被通用 buildPayload 引用(L1426,传 Transaction 范围);
  OP 组合根下该调用在 `if constexpr (!c_opMode)` 内被丢弃,不实例化、不冲突;但**若将来 OP 化
  块构建**,真实 executeBlock 期望 raw bytes 而 buildPayload 传 Transaction 范围 → 实例化即编译错。
  本期不做 OP 化块构建,记为决策记录。
- **测试不可退步(强制)**:现有 t8n/e2e/detail-tests 全绿;新测试只加强。

### 明确不做

- ❌ 重写 processOpBlock 执行逻辑(对齐 op-geth 不变)。
- ❌ 并行/chunk(串行,已证伪)。
- ❌ 引入失败回执(破坏对拍)。
- ❌ OpSchedulerImpl 实现 SchedulerInterface / MultiVersionScheduler slot 换人(OpCallScheduler 独占 slot 3)。
- ❌ opEnvelopeToTars 物理搬入 opstack-executor(转换器注入替代,opstack-executor 保持 rpc-free)。
- ❌ OP 化块构建(buildPayload for OP,前向依赖见上,本期不做)。

## 文件清单

| 文件 | 改动 |
|---|---|
| `opstack-executor/OpSchedulerImpl.h` | 模板加第二参 `OpenedStorage=Storage`;构造注入 blockFactory+storage+converter;executeBlock 变真 + commitBlock 变真 + pendingExecuteResult + resetPending + m_pending(按值 ViewType);更新 notifier 权威注释("engine 触发"→"commitBlock 触发") |
| `opstack-executor/OpBlockRegister.h` | **新增** `opstackRegisterBlock` 纯函数 + `EnvelopeToTarsConverter`(从 engine registerOpBlock 下沉;blockHash 显式传参;converter 注入) |
| `engine/bcos-engine/EngineServiceImpl.h` | newPayload 三阶段;删 registerOpBlock/mergeView/notifier;executeBlock 前获取 ledgerConfig;比对 INVALID 分支 resetPending |
| `libinitializer/Initializer.cpp` | `OpSchedulerImpl<ViewType, GlobalStateStorage>` 构造传 blockFactory+storage+converter(lambda 调 `bcos::engine::detail::opEnvelopeToTars`) |
| `opstack-executor/tests/` | opstackRegisterBlock 单测 + 两阶段单测(注册进 `opstack-executor-block-tests`);5 处模板/构造实例化点更新 |
| `docs/2026-08-11-op-driver-orchestration-alignment-design.md` | 本文档 |

**不动的文件**(审查修正,原设计曾列):`opEnvelopeToTars` 及其前向声明(留 engine)、
`opstack-executor/CMakeLists.txt`(不加 rpc)、`engine/CMakeLists.txt`(不改)、
`OpEnvelopeToTarsTest`(不迁移)、`OpSchedulerImpl.h` 模板首参(概念钉死)。

## 决策记录

- 2026-08-11:用户确认形态 B(两阶段 executeBlock/commitBlock),D1 方案③(写块表下沉为
  opstack-executor 纯函数 + opEnvelopeToTars 共享)——参考 ethereum 的 prewriteBlockToBuffer 在
  ledger 通用层。
- 2026-08-11 四维审查修订(合并全部审查意见):
  ① 模板首参保持 `Storage`(=ViewType,概念钉死),**新增第二模板参 OpenedStorage**(CRITICAL-1);
  ② `m_pending` 按值持有 ViewType,**不存指针**(CRITICAL-2,UAF);
  ③ commitBlock 定死为 `task::Task<void>(BlockHeader::Ptr, HashType)` seam 方法,非
     SchedulerInterface override;删除"任何 SchedulerInterface 持有者都能驱动"目标表述;
  ④ opEnvelopeToTars **留 engine,转换器注入**(替代"加 rpc link"——rpc 拖 jsoncpp/jwt/tars-
     servant 进 EVM 层模块,且 install(EXPORT)/PUBLIC include 有坑;注入与 setBlockNumberNotifier/
     OpCallScheduler 注入模式同构);
  ⑤ blockHash 显式传参,不搬 opHeaderConst(避免常量三处漂移);
  ⑥ rawTxBytes 为 `std::vector<bcos::bytes>`(非单字节串);ledgerConfig 由 engine 经
     getLedgerConfig 获取(OP 路径现状无此对象);INVALID 分支 resetPending;
  ⑦ 双 fork 消除(engine 一次 fork 传入 executeBlock);单槽串行不变量成文;
  ⑧ e2e 例数修正(80,非 65);测试名修正;测试 target 明确为 opstack-executor-block-tests;
  ⑨ 前向依赖注记:OP 化块构建会击穿 executeBlock 的 Transaction 范围实例化(本期不做)。
