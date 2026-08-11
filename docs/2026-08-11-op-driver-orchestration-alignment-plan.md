# OP 驱动编排层对齐 — executeBlock/commitBlock 两阶段 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 OP 块执行对齐到 ethereum 的两阶段结构——`OpSchedulerImpl::executeBlock` 变真(执行+暂存)、`commitBlock` 变真(写块表+mergeView+notifier),engine 的 OP 分支瘦身为"execute → 比对 → commit"三阶段。

**Architecture:** 写块表从 engine 的 `registerOpBlock`(EngineServiceImpl.h:1204-1323)下沉为 opstack-executor 纯函数 `opstackRegisterBlock`,envelope→tars 转换经 `EnvelopeToTarsConverter` 回调注入(opstack-executor 保持 rpc-free);`OpSchedulerImpl` 模板加第二参 `OpenedStorage` 承载落盘目标;engine 经 SchedulerType 依赖名触达新表面(seam 纯度不变)。

**Tech Stack:** bcos-task 协程、bcos-storage2(MultiLayerStorage fork/mergeView)、bcos-ledger(SYS_* 表/getLedgerConfig)、protocol::BlockFactory、Boost 异常 DERIVE_BCOS_EXCEPTION、Boost.Test(block-tests)。

## Global Constraints

- **测试不可退步(强制)**:现有 t8n 127 / e2e 80(74 newPayload + 6 forkchoice)/ detail-tests / test-bcos-engine / test-transaction-scheduler 全绿;任何任务不得使其变红。测试只加强。
- **engine 隔离(seam 纯度)**:EngineServiceImpl.h 是 public/install 模板头,永不 include bcos-evm/opstack-executor 类型;经 `SchedulerType::` 依赖名触达。新增表面同理。
- **opstack-executor 不得依赖 bcos-engine / bcos-rpc**(只允许经构造注入的 std::function 回调跨边界)。
- **错误分类**:ConsensusError→INVALID、StorageError→-32603、内部错误→-32603(OpExecutionInternalError);"本地故障不得投票反对块"。
- `executeBlock` 签名必须满足 `scheduler_v1::TransactionScheduler` 概念:`executeBlock(Storage&, executor, BlockHeader const&, input_range, LedgerConfig const&)` 返回 `task::Task<vector<Receipt::Ptr>>`。
- `m_pending` 按值持有 `ViewType`(协程局部指针即悬垂);commitBlock 前 engine 不得再触碰已 move 的 view。
- 提交一律 `git commit --no-verify`(clang-format 17 钩子漂移)。

---

### Task 1: `OpExecutionInternalError` 下沉到 bcos-framework

**Files:**
- Create: `bcos-framework/bcos-framework/engine/Errors.h`
- Modify: `engine/bcos-engine/EngineServiceImpl.h:92`(删除本地 DERIVE 定义)、`:40-42` include 区
- Verify: `cmake --build build --target init` + `cmake --build build --target opstack-executor-block-tests`

**Interfaces:**
- Consumes: `bcos-utilities/Exceptions.h` 的 `DERIVE_BCOS_EXCEPTION`(bcos-framework 已 link bcos-utilities,`ledger/Features.h` 等已有同样 include)
- Produces: `bcos::engine::OpExecutionInternalError`(与现状同名同命名空间,迁移后所有既有抛点/捕获点零改动)——Task 2/3 从 opstack-executor 侧 throw 它的前提

- [ ] **Step 1: 新建 `bcos-framework/bcos-framework/engine/Errors.h`**

```cpp
#pragma once

#include <bcos-utilities/Exceptions.h>

namespace bcos::engine
{
/// JSON-RPC -32603 "Internal error": an OP block execution failure the error-classification table
/// attributes to the storage layer rather than to the block. Must never be reported as INVALID --
/// a storage fault is not a consensus verdict on the payload. Lives in bcos-framework (not the
/// engine library) so opstack-executor can throw it without depending on bcos-engine.
DERIVE_BCOS_EXCEPTION(OpExecutionInternalError);
}  // namespace bcos::engine
```

- [ ] **Step 2: 删除 engine 本地定义**

在 `engine/bcos-engine/EngineServiceImpl.h:92` 删除 `DERIVE_BCOS_EXCEPTION(OpExecutionInternalError);`(保留 `OpPayloadBuildingUnsupported`)。在文件 include 区(约 L40-42,`#include "bcos-utilities/Exceptions.h"` 附近)新增 `#include <bcos-framework/engine/Errors.h>`。

- [ ] **Step 3: 编译验证(纯重定位,零行为变化)**

Run: `cmake --build build --target init`(生产组合根,静态断言)
Expected: 构建成功(同一类型同名同命名空间,所有 `OpExecutionInternalError` 抛点/捕获点不变)

- [ ] **Step 4: 回归**

Run: `cmake --build build --target opstack-executor-block-tests` + `ctest --test-dir build -R OpstackExecutorBlockTests`
Expected: 全绿(e2e 测试含 `BOOST_CHECK_THROW(..., bcos::engine::OpExecutionInternalError)`,类型未变)

- [ ] **Step 5: Commit**

```bash
git add bcos-framework/bcos-framework/engine/Errors.h engine/bcos-engine/EngineServiceImpl.h
git commit --no-verify -m "refactor(engine): move OpExecutionInternalError to bcos-framework/engine/Errors.h"
```

---

### Task 2: `opstackRegisterBlock` 纯函数 + `EnvelopeToTarsConverter` 注入 + 单测

**Files:**
- Modify: `opstack-executor/OpErrors.h`(`OpExecuteBlockResult` 从 OpSchedulerImpl.h 移入,新增 include)
- Modify: `opstack-executor/OpSchedulerImpl.h:56-75`(删除本地 `OpExecuteBlockResult` 定义,保留 seam 别名)
- Create: `opstack-executor/OpBlockRegister.h`(纯函数 + 转换器别名)
- Create: `opstack-executor/tests/OpBlockRegisterTest.cpp`
- Modify: `opstack-executor/tests/CMakeLists.txt`(把新测试加进 `opstack-executor-block-tests`)

**Interfaces:**
- Consumes: Task 1 的 `bcos::engine::OpExecutionInternalError`;`bcos::evm::opstack::OpBlockSeal`(OpExecuteBlockResult.seal 成员)
- Produces:
  - `using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(bcos::bytes const&, bcos::crypto::HashType const&)>;`(namespace `bcos::evm::engine`)
  - `template <class ViewType> inline task::Task<void> opstackRegisterBlock(ViewType& view, bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash, std::vector<bcos::bytes> const& rawTxBytes, OpExecuteBlockResult const& result, bcos::protocol::BlockFactory const& blockFactory, EnvelopeToTarsConverter const& envelopeToTars);`(namespace `bcos::evm::engine`)
  - `OpExecuteBlockResult`(自 OpSchedulerImpl.h:68-75 移入 OpErrors.h,namespace `bcos::evm::engine` 不变)

- [ ] **Step 1: 移动 `OpExecuteBlockResult` 到 OpErrors.h**

把 OpSchedulerImpl.h:68-75 的 `OpExecuteBlockResult` struct 移入 `opstack-executor/OpErrors.h`(namespace `bcos::evm::engine`,加在 OpStorageError 之后)。OpErrors.h 补 include:`<bcos-crypto/hash/hash.h>`(bcos::h256)、`<bcos-evm/opstack/OpBlockSeal.h>`(seal 成员)、`<bcos-framework/protocol/TransactionReceipt.h>`、`<vector>`。OpSchedulerImpl.h 删除本地定义(它已 include OpErrors.h,类型仍可见;`using ExecuteResult = OpExecuteBlockResult;` 别名不动)。

- [ ] **Step 2: 新建 `opstack-executor/OpBlockRegister.h`**

```cpp
#pragma once

#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/executor/StateKey.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <opstack-executor/OpErrors.h>
#include <boost/lexical_cast.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm::engine
{
/// raw EIP-2718 envelope -> tars Transaction。签名与 engine 的
/// `bcos::engine::detail::opEnvelopeToTars` 一致;由 composition root(Initializer)注入 lambda
/// 调用之——opstack-executor 不 link bcos-rpc / bcos-engine。
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;

/// 写块表:等效 ethereum 的 ledger::prewriteBlockToBuffer。从 engine registerOpBlock
/// (EngineServiceImpl.h:1204-1323)逐行搬移,数据来源改为显式参数。5 张表:
///   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
///   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
/// 失败分类:receipt 数量不变量 / null receipt / 存储写失败 -> OpExecutionInternalError
/// (engine 屏障原样放行 -> -32603)。blockHash 由调用方显式传入(engine step 2 已校验
/// == header.opHeaderHash(opHeaderConst()),不在此重算,避免常量漂移)。
template <class ViewType>
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash,
    std::vector<bcos::bytes> const& rawTxBytes, OpExecuteBlockResult const& result,
    bcos::protocol::BlockFactory const& blockFactory, EnvelopeToTarsConverter const& envelopeToTars)
{
    const auto blockNumberStr = boost::lexical_cast<std::string>(header.number());

    bcos::storage::Entry numberToHashEntry;
    numberToHashEntry.set(blockHash.asBytes());
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr},
        std::move(numberToHashEntry));

    bcos::storage::Entry hashToNumberEntry;
    hashToNumberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(hashToNumberEntry));

    // OP header 以 tars BlockHeader 落标准 s_number_2_header 表(与普通 FISCO 块同表同格式)。
    // dataHash 为空 -> header.hash() 抛 EmptyBlockHeaderHash;本路径不调用它。encode() 是
    // `void encode(bytes&)` out-param(BlockHeader.h:50)——先建 buffer 再取。
    bcos::storage::Entry headerEntry;
    bcos::bytes headerBuffer;
    header.encode(headerBuffer);
    headerEntry.set(std::move(headerBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));

    auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
    // processOpBlock 每 tx 恰产一 receipt;数量分叉是执行层坏不变量,响亮失败(内部错误,非对块的裁决)。
    if (rawTxBytes.size() != result.receipts.size())
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                              << bcos::errinfo_comment{
                                  "OP block execution returned a receipt count differing from "
                                  "the transaction count"});
    }
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        auto const& receipt = result.receipts[index];
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                                  << bcos::errinfo_comment{
                                      "OP block execution returned a null receipt"});
        }
        bcos::bytes encodedReceipt;
        receipt->encode(encodedReceipt);
        const auto txHash = hashImpl.hash(rawTxBytes[index]);

        bcos::storage::Entry receiptEntry;
        receiptEntry.set(std::move(encodedReceipt));
        co_await bcos::storage2::writeOne(view,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)},
            std::move(receiptEntry));

        // 转换失败(畸形/未枚举 envelope)-> 行跳过,块仍 VALID、该 tx 不可按 hash 查。
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            bcos::bytes encodedTx;
            txImpl.encode(encodedTx);
            bcos::storage::Entry txEntry;
            txEntry.set(std::move(encodedTx));
            co_await bcos::storage2::writeOne(view,
                bcos::executor_v1::StateKey{
                    bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)},
                std::move(txEntry));
        }
    }
}
}  // namespace bcos::evm::engine
```

> 注:`bcos::storage::Entry` / `bcos::storage2::writeOne` / `bcos::executor_v1::StateKey` /
> `bcos::ledger::SYS_*` / `bcos::concepts::bytebuffer::toView` 均来自 bcos-framework(已 link)。
> 若 `LedgerTypeDef.h` 未暴露 `SYS_*` 常量名,改用 engine 原代码相同 include 路径
> (EngineServiceImpl.h 的 include 列表)。

- [ ] **Step 3: 写失败测试(先红)**

新建 `opstack-executor/tests/OpBlockRegisterTest.cpp`(Boos.Test,注册进 `opstack-executor-block-tests`):

```cpp
#include <opstack-executor/OpBlockRegister.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-protocol/TransactionReceipt.h>  // TransactionReceiptImpl 路径
#include <bcos-protocol/BlockFactoryImpl.h>    // BlockHeaderFactoryImpl 同头族
#include <boost/test/unit_test.hpp>
// 复用 e2e 的 makeCryptoSuite/makeBlockFactory 等价 helper(keccak hashImpl,见
// OpNewPayloadRpcE2eTest.cpp:80-128)。测试文件内自建一份,避免跨文件依赖。

namespace
{
using BackendMemStorage = bcos::storage::MemoryStorage;  // 依测试既有别名
// ... 按 OpNewPayloadRpcE2eTest 的 MLS/ViewType 别名定义
}
```

Test A **HappyFiveTables**:构造 `multiLayerStorage.fork()` + `newMutable()`;header(makeOpHeader 模式,number=1);`rawTxBytes` = 1 条伪造 envelope(如 `bcos::bytes{0x7e, 0x02, /*...*/}`——fake converter 不解码,内容无关);`result.receipts` = 1 条(经 receiptFactory 建 `TransactionReceiptImpl`,set status);`blockHash` = 任意 `bcos::h256`;fake converter 返回固定 `bcostars::Transaction`(填 sender + extraTransactionHash 任意字节)。调用 `syncWait(opstackRegisterBlock(view, *header, blockHash, rawTxBytes, result, blockFactory, converter))` 后逐表 `storage2::readOne` 断言:
- `SYS_NUMBER_2_HASH["1"]` 值 == `blockHash.asBytes()`
- `SYS_HASH_2_NUMBER[blockHash]` 值 == `"1"`
- `SYS_NUMBER_2_BLOCK_HEADER["1"]` 值 == `header.encode()` 的字节(`blockFactory->blockHeaderFactory()->createBlockHeader(bytes)` 反解后 `number()==1`)
- `SYS_HASH_2_RECEIPT[txHash]` 值 == `receipt->encode()`(`txHash = blockFactory.cryptoSuite()->hashImpl()->hash(rawTxBytes[0])`)
- `SYS_HASH_2_TX[txHash]` 存在,反解 `createTransaction` 后 `extraTransactionHash == txHash`

Test B **ReceiptCountMismatch**:`rawTxBytes` 2 条但 receipts 1 条 → `BOOST_CHECK_THROW(syncWait(...), bcos::engine::OpExecutionInternalError)`。

Test C **NullReceipt**:1 条 envelope、receipts[0]=nullptr → `OpExecutionInternalError`。

Test D **ConverterNulloptSkipsTxRow**:converter 返回 `std::nullopt` → happy 路径 4 张表(除 SYS_HASH_2_TX)有值,`SYS_HASH_2_TX[txHash]` 无值(块仍有效语义)。

- [ ] **Step 4: 注册测试 target**

`opstack-executor/tests/CMakeLists.txt` 的 `opstack-executor-block-tests` 源文件列表(L26-38)加入 `OpBlockRegisterTest.cpp`。该 target 已 link `opstack-executor ... codec protocol-tars ledger engine bcos-utilities ... rpc`——fake converter 不需要 engine,但 target 已带,无额外改动。

- [ ] **Step 5: 跑测试(转绿)**

Run: `cmake --build build --target opstack-executor-block-tests` + `./build/opstack-executor/tests/opstack-executor-block-tests --run_test=OpBlockRegisterSuite`
Expected: 4 个用例全过;其余既有 block-tests 用例不回归(OpExecuteBlockResult 移动是同名同命名空间重定位)

- [ ] **Step 6: Commit**

```bash
git add opstack-executor/OpErrors.h opstack-executor/OpBlockRegister.h \
        opstack-executor/OpSchedulerImpl.h opstack-executor/tests/OpBlockRegisterTest.cpp \
        opstack-executor/tests/CMakeLists.txt
git commit --no-verify -m "feat(opstack): sink registerOpBlock to opstackRegisterBlock pure fn + injected envelope converter"
```

---

### Task 3: OpSchedulerImpl 两阶段表面(executeBlock/commitBlock 变真)+ 全部实例化点

**Files:**
- Modify: `opstack-executor/OpSchedulerImpl.h`(模板、构造、PendingBlock、executeBlock、commitBlock、pendingExecuteResult、resetPending、notifier 注释)
- Modify: `opstack-executor/tests/OpSchedulerImplSmokeTest.cpp:106、151`
- Modify: `opstack-executor/tests/OpEngineBranchSmokeTest.cpp:95、110`
- Modify: `opstack-executor/tests/OpL1EdgeGateTest.cpp:164`
- Modify: `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp:140-166`(fixture 构造 + 成员重排)
- Modify: `libinitializer/Initializer.cpp:485-487`(生产实例化)
- Create: `opstack-executor/tests/OpTwoPhaseTest.cpp`
- Modify: `opstack-executor/tests/CMakeLists.txt`(加 OpTwoPhaseTest.cpp)

**Interfaces:**
- Consumes: Task 2 的 `opstackRegisterBlock` + `EnvelopeToTarsConverter`;Task 1 的 `bcos::engine::OpExecutionInternalError`;`ViewType` 的 `newMutable()/fork()`、`OpenedStorage::mergeView(ViewType)`(MultiLayerStorage.h:555,按值消费、可移动)
- Produces:
  - `template <class Storage, class OpenedStorage = Storage> class OpSchedulerImpl`
  - 构造 `OpSchedulerImpl(receiptFactory, chainId, forkTimestamps, protocol::BlockFactory::Ptr blockFactory, OpenedStorage& storage, EnvelopeToTarsConverter envelopeToTars)`
  - `task::Task<std::vector<TransactionReceipt::Ptr>> executeBlock(Storage& view, auto& executor, BlockHeader const& header, ::ranges::input_range auto const& rawTxBytes, LedgerConfig const& ledgerConfig)`(变真:执行→暂存)
  - `task::Task<void> commitBlock(BlockHeader::Ptr header, bcos::crypto::HashType const& blockHash)`(变真:写表→mergeView→notifier)
  - `OpExecuteBlockResult const& pendingExecuteResult() const`、`void resetPending()`
  - `struct PendingBlock { ViewType view; std::vector<bcos::bytes> rawTxBytes; OpExecuteBlockResult result; bcos::protocol::BlockNumber blockNumber; };`
  - `std::optional<PendingBlock> m_pending;`
  - 成员 `protocol::BlockFactory::Ptr m_blockFactory; OpenedStorage& m_storage; EnvelopeToTarsConverter m_envelopeToTars;`

- [ ] **Step 1: 类模板 + 构造 + 成员**

`opstack-executor/OpSchedulerImpl.h`:
- include 区补:`#include <opstack-executor/OpBlockRegister.h>`、`#include <bcos-framework/engine/Errors.h>`(两者分别来自 Task 2 / Task 1)
- L86 `template <class Storage>` → `template <class Storage, class OpenedStorage = Storage>`
- 构造 L90-96 扩为 6 参,初始化 `m_blockFactory`/`m_storage`/`m_envelopeToTars`
- L98-104 notifier 注释改为:"The scheduler fires `notifyBlockNumber` inside `commitBlock` after the view is merged(originally the engine fired it after its own mergeView; the two-phase refactor moved the firing into the commit point)"

- [ ] **Step 2: executeBlock 变真(替换 L211-220 哑桩)**

```cpp
/// 两阶段 phase 1:执行并把结果暂存进 m_pending(所有权转移——从参数 move view 进 pending;
/// 调用方在返回后不得再触碰该 view)。ledgerConfig 仅满足概念签名,OP 执行不消费。
task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
    Storage& view, auto& /*executor*/, bcos::protocol::BlockHeader const& header,
    ::ranges::input_range auto const& rawTxBytes,
    bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
{
    auto result = co_await executeOpBlock(view, header, rawTxBytes);
    std::vector<bcos::bytes> materialized(std::begin(rawTxBytes), std::end(rawTxBytes));
    m_pending = PendingBlock{
        std::move(view), std::move(materialized), std::move(result), header.number()};
    co_return m_pending->result.receipts;
}
```

- [ ] **Step 3: pendingExecuteResult / resetPending / commitBlock(新增,置于 executeOpBlock 之后)**

```cpp
/// engine 比对拿 commitments 的通道;无待提交结果即内部错误(调用契约:engine 恒在 executeBlock
/// 之后调用)。
OpExecuteBlockResult const& pendingExecuteResult() const
{
    if (!m_pending)
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                              << bcos::errinfo_comment{
                                  "pendingExecuteResult: no pending executeBlock result"});
    }
    return m_pending->result;
}

/// 比对 INVALID 分支调用:清残留,避免持有带 mutable 层的视图到下一块。
void resetPending() { m_pending.reset(); }

/// 两阶段 phase 2:落盘(写块表 + mergeView 原子合并)+ notifier。seam 方法,非
/// SchedulerInterface override——仅 engine 经 SchedulerType 依赖名触达。
task::Task<void> commitBlock(bcos::protocol::BlockHeader::Ptr header,
    bcos::crypto::HashType const& blockHash)
{
    if (!m_pending)
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                              << bcos::errinfo_comment{
                                  "commitBlock: no pending executeBlock result"});
    }
    if (header->number() != m_pending->blockNumber)
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                              << bcos::errinfo_comment{
                                  "commitBlock: header block number does not match the pending "
                                  "execution"});
    }
    co_await opstackRegisterBlock(m_pending->view, *header, blockHash, m_pending->rawTxBytes,
        m_pending->result, *m_blockFactory, m_envelopeToTars);
    co_await m_storage.mergeView(std::move(m_pending->view));
    m_pending.reset();
    notifyBlockNumber(header->number());
}
```

> `co_await m_storage.mergeView(...)` 需要 `OpenedStorage` 是具备 `mergeView(ViewType)` 的类型
> (MultiLayerStorage);`OpenedStorage=Storage`(默认)时仅执行不 commit 的实例化不实例化该方法体,
> 故 execute-only 测试可编译。生产/两阶段测试显式给 `OpenedStorage`。

- [ ] **Step 4: 更新 4 个测试实例化点(构造 3→6 参 + 类型别名第二模板参,保持编译绿)**

> **关键**:三个 e2e 风格的测试文件里,`OpSchedulerImpl<ViewType>` 裸别名(默认 `OpenedStorage=ViewType`)
> 在 Task 3 能编译(engine 尚未调 commitBlock,其函数体不实例化),但 Task 4 后 engine 的
> `runOpNewPayloadSteps` 会调 `commitBlock` → 强制实例化其函数体 → `ViewType` 无 `mergeView` 编译失败。
> 因此本步必须把别名一并改为 `OpSchedulerImpl<ViewType, MLS>`。

- `OpSchedulerImplSmokeTest.cpp:106、151`:本地构造(无 engine)
  `OpSchedulerImpl<ViewType> scheduler(nullptr, 0x2105, fork)` →
  `OpSchedulerImpl<ViewType, MLS> scheduler(nullptr, 0x2105, fork, nullptr, multiLayerStorage, {})`
  (测试已构造 `MLS multiLayerStorage` 在作用域;blockFactory=nullptr、converter=空,两条 smoke
  路径不触碰;不引入 bcos-crypto,保住该测试"nullptr receiptFactory 免拖 bcos-crypto"的意图)。
- `OpEngineBranchSmokeTest.cpp`:
  - L94-95 类型别名 `OpSchedulerImpl<ViewType>` → `OpSchedulerImpl<ViewType, MLS>`(此测试靠
    实例化 `OpEngine` 强制 body 实例化,Task 4 后必须 MLS 承载 mergeView);
  - L110 本地构造 → `OpSchedulerImpl<ViewType, MLS> scheduler(nullptr, 0x2105, fork, nullptr, storage, {})`
    (`MLS storage` 在 L107 作用域;blockFactory 传 nullptr——scheduler 的 blockFactory 仅供
    commitBlock,此测试不 commit)。
- `OpL1EdgeGateTest.cpp`:
  - L164 别名 `using OpScheduler = ...OpSchedulerImpl<ViewType>` → `...OpSchedulerImpl<ViewType, MLS>`;
  - fixture(L168-185)**成员重排**:`bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};`
    移到 `OpScheduler scheduler;` 之前(成员按声明序初始化);
  - L180-181 构造 → `scheduler(receiptFactory, kChainId, forkTimestamps, blockFactory, multiLayerStorage, realConverter)`。
- `OpNewPayloadRpcE2eTest.cpp`:
  - L140 别名 `using OpScheduler = ...OpSchedulerImpl<ViewType>` → `...OpSchedulerImpl<ViewType, MLS>`;
  - fixture(L144-166)**成员重排**同上(blockFactory 移到 scheduler 之前);
  - L162-163 构造 → `scheduler(receiptFactory, kChainId, forkTimestamps, blockFactory, multiLayerStorage, realConverter)`。

两处 fixture 的 `realConverter` 均为 lambda 调 engine 的 `opEnvelopeToTars`(block-tests 已 link
engine,前向声明在 EngineServiceImpl.h:168):
```cpp
[](bcos::bytes const& env, bcos::crypto::HashType const& h) -> std::optional<bcostars::Transaction> {
    return bcos::engine::detail::opEnvelopeToTars(env, h);
}
```
engine 尚未三阶段化(直到 Task 4),此 converter 暂不被旧路径使用,无副作用。

- [ ] **Step 5: 更新生产实例化点**

`libinitializer/Initializer.cpp:485-487`:
```cpp
auto opScheduler =
    std::make_shared<bcos::evm::engine::OpSchedulerImpl<GlobalStateStorage::ViewType,
        GlobalStateStorage>>(m_protocolInitializer->blockFactory()->receiptFactory(), opChainId,
        forkTimestamps, m_protocolInitializer->blockFactory(),
        m_globalStateStorageInitializer->storage(),
        [](bcos::bytes const& env, bcos::crypto::HashType const& h)
            -> std::optional<bcostars::Transaction> {
            return bcos::engine::detail::opEnvelopeToTars(env, h);
        });
```
(Initializer 已 include EngineServiceImpl.h → `bcos::engine::detail::opEnvelopeToTars` 前向声明可见;`m_globalStateStorageInitializer->storage()` 返回 `GlobalStateStorage&`。L513-517 的 `OpEngineServiceT::c_opMode` static_assert 用 `std::remove_reference_t<decltype(*opScheduler)>` 取裸类型,第二模板参不影响探测。)

- [ ] **Step 6: 两阶段单测(新建 `OpTwoPhaseTest.cpp`,注册进 block-tests)**

Fixture 同 e2e(MLS + makeBlockFactory + real converter)。用例:
- **HappyExecuteCommit**:`setBlockNumberNotifier` 计数;`executeBlock(view, exec, *header, txs, ledgerConfig)`(view=multiLayerStorage.fork()+newMutable,ledgerConfig 默认构造)→ 返回 receipts;`pendingExecuteResult()` 有值;`commitBlock(header, blockHash)`(blockHash 任意 h256)→ 返回后:MLS 新 fork 上 `SYS_NUMBER_2_HASH` 有值(证明 mergeView 落盘)、`pendingExecuteResult()` 抛 `OpExecutionInternalError`(m_pending 已清)、notifier 计数==1。
- **CommitEmptyPending**:不 execute 直接 `commitBlock` → `OpExecutionInternalError`。
- **CommitNumberMismatch**:execute(header number=1)→ `commitBlock` 传 number=2 的 header → `OpExecutionInternalError`。
- **ChainedAfterCommit**:execute#1→commit→execute#2→commit(两次都过)。
- **ChainedAfterReset**:execute→`resetPending()`→execute→commit(自愈)。

- [ ] **Step 7: 编译 + 全量回归**

Run: `cmake --build build --target opstack-executor-block-tests opstack-executor-detail-tests init`
Expected: 全部编译通过(e2e 旧路径不变,80 例仍走 executeOpBlock+engine registerOpBlock)
Run: `ctest --test-dir build -R "OpstackExecutor(BlockTests|DetailTests)|BcosEvm"`
Expected: 全绿——既有用例零退步 + 新两阶段用例全过

- [ ] **Step 8: Commit**

```bash
git add opstack-executor/OpSchedulerImpl.h opstack-executor/tests/OpTwoPhaseTest.cpp \
        opstack-executor/tests/OpSchedulerImplSmokeTest.cpp opstack-executor/tests/OpEngineBranchSmokeTest.cpp \
        opstack-executor/tests/OpL1EdgeGateTest.cpp opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp \
        opstack-executor/tests/CMakeLists.txt libinitializer/Initializer.cpp
git commit --no-verify -m "feat(opstack): two-phase executeBlock/commitBlock on OpSchedulerImpl + injected storage/converter"
```

---

### Task 4: engine newPayload 三阶段(OP 分支瘦身)

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`:
  - `runOpNewPayloadSteps`(L1096-1182)Step 4/5/6 改写
  - 删除 `registerOpBlock`(L1204-1323)
- Verify: `cmake --build build --target engine init` + e2e 80 例

**Interfaces:**
- Consumes: Task 3 的 `executeBlock`/`pendingExecuteResult`/`resetPending`/`commitBlock`;`bcos::ledger::getLedgerConfig(view, ledgerConfig, blockNumber, blockFactory)`(buildPayload L1378 已有同款调用);`detail::opEnvelopeToTars`(保留在 engine,被 Initializer 注入,勿删)

- [ ] **Step 1: Step 4 改写为"取 ledgerConfig + executeBlock"**

把 L1096-1154(execute 段)整体替换为:

```cpp
// ---- Step 4: execute(two-phase phase 1)----
view.newMutable();
// OP 现状不消费 ledgerConfig(executeOpBlock 不带),仅供概念合规;获取路径沿用 buildPayload
// 的 ledger::getLedgerConfig(L1377-1378),按"配置生效到父块"语义取 payload.blockNumber-1。
bcos::ledger::LedgerConfig ledgerConfig;
// payload.blockNumber 是 protocol::BlockNumber(int64),无需转换。
co_await bcos::ledger::getLedgerConfig(view, ledgerConfig, payload.blockNumber - 1, *m_blockFactory);
std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
try
{
    // executeBlock 从参数 move view 进 m_pending——此后本分支不得再触碰 view(比对走
    // pendingExecuteResult,提交走 commitBlock 内部)。
    receipts = co_await m_scheduler.get().executeBlock(
        view, m_executor.get(), *ethHeader, *payload.rawTransactions, ledgerConfig);
}
// 错误分类表不变(两 catch 体无 co_await——[expr.await]/2)
catch (const typename SchedulerType::ConsensusError& e)
{
    co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
        std::string("OP block execution rejected the payload: ") + e.what());
}
catch (const typename SchedulerType::StorageError& e)
{
    BOOST_THROW_EXCEPTION(
        OpExecutionInternalError{} << bcos::errinfo_comment{
            std::string("OP block execution hit a storage failure: ") + e.what()});
}
catch (...)
{
    BOOST_THROW_EXCEPTION(
        OpExecutionInternalError{} << bcos::errinfo_comment{
            "OP block execution threw an unclassified exception (typed classification "
            "bypassed)"});
}
```

- [ ] **Step 2: Step 5 比对改用 pendingExecuteResult + INVALID 分支 resetPending**

把 L1156-1168(比对段)替换为:

```cpp
// ---- Step 5: 八项比对(pendingExecuteResult 拿 ExecuteResult → commitments)----
const auto& executeResult = m_scheduler.get().pendingExecuteResult();
const auto commitments = SchedulerType::commitmentsOf(executeResult);
const auto announced =
    SchedulerType::announcedCommitmentsOf(payload, transactionsRoot, *ethHeader);
if (auto mismatchedField = SchedulerType::mismatchedFieldOf(commitments, announced);
    mismatchedField.has_value())
{
    m_scheduler.get().resetPending();  // 不留残留视图到下一块
    co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
        std::string("execution result does not match payload field: ") + *mismatchedField);
}
```

- [ ] **Step 3: Step 6 改为 commitBlock**

把 L1170-1182(注册+merge+notify)替换为:

```cpp
// ---- Step 6: commit(two-phase phase 2)----
// commitBlock 内部:opstackRegisterBlock 写 5 表 → mergeView 原子落盘 → notifyBlockNumber。
// 落盘错误经 handleOpNewPayload 屏障(OpExecutionInternalError 原样放行 → -32603)。
co_await m_scheduler.get().commitBlock(ethHeader, payload.blockHash);
co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
```

- [ ] **Step 4: 删除 registerOpBlock 方法**

删除 L1204-1323 的 `registerOpBlock` 模板方法(其逻辑已下沉为 opstack-executor 的
`opstackRegisterBlock`,Task 2)。同时检查:engine 头内 `detail::opEnvelopeToTars` 前向声明
(L168-169)与 .cpp 实现(L33-58)**保留**(被 Initializer 的转换器 lambda 引用,勿删)。

- [ ] **Step 5: 编译 + e2e 回归**

Run: `cmake --build build --target init engine opstack-executor-block-tests`
Expected: 编译通过(engine 头不再拼 executeOpBlock;`view` 在 executeBlock 后被 move,无后续引用)
Run: `ctest --test-dir build -R OpstackExecutorBlockTests`
Expected: **e2e 80 例全绿**——这是三阶段重构的真正回归闸(74 newPayload + 6 forkchoice;比对
INVALID、已知块短路、forkchoice 路径均不触发 notifier 的断言照旧)

- [ ] **Step 6: 其余回归**

Run: `ctest --test-dir build -R "OpstackExecutorDetailTests|BcosEvm|Engine"` + `./build/transaction-scheduler/tests/test-transaction-scheduler`(若有)
Expected: detail-tests(比对纯函数)、test-bcos-engine(通用 scheduler)、test-transaction-scheduler(真实通用 engine)全绿——三阶段只动 OP 分支,通用路径字节级不变

- [ ] **Step 7: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h
git commit --no-verify -m "feat(engine): three-phase OP newPayload (executeBlock -> compare -> commitBlock); drop registerOpBlock"
```

---

### Task 5: 全量回归 + 文档收尾

**Files:**
- Modify: `docs/2026-08-11-op-driver-orchestration-alignment-design.md`(追加实施记录)

**Interfaces:**
- Consumes: Task 1-4 全部交付

- [ ] **Step 1: 全量构建**

Run: `cmake --build build -j8`
Expected: 全仓库构建成功(含 init / engine / opstack-executor / block-tests / detail-tests / 其他目标)

- [ ] **Step 2: 全量测试**

Run: `ctest --test-dir build`
Expected: 全绿。重点核对无退步:t8n 127(执行零改动,executeOpBlock 保留)、e2e 80(三阶段)、
detail-tests(比对)、test-bcos-engine、test-transaction-scheduler、opstack-executor-tests、
bcos-evm 相关。

- [ ] **Step 3: seam 纯度复查(grep)**

Run:
```bash
grep -n "bcos-evm\|opstack" engine/bcos-engine/EngineServiceImpl.h | grep -v "SchedulerType\|c_opMode\|OpEngineSeam\|detail::opEnvelopeToTars\|OpExecutionInternalError\|registerOpBlock 已删\|comment" || true
grep -n "OpExecutionInternalError\|mergeView\|notifyBlockNumber" engine/bcos-engine/EngineServiceImpl.h
```
Expected:engine 头不拼写 bcos-evm/opstack-executor 类型名;`mergeView`/`notifyBlockNumber` 在
engine 侧消失(移入 commitBlock);`registerOpBlock` 消失;`opEnvelopeToTars` 前向声明仍在。
另查无残留裸别名:
```bash
grep -rn "OpSchedulerImpl<ViewType>" opstack-executor/tests/ libinitializer/ || true
```
Expected:无输出(全部为 `OpSchedulerImpl<ViewType, MLS>` / `<ViewType, GlobalStateStorage>`)。

- [ ] **Step 4: 文档收尾**

在 `docs/2026-08-11-op-driver-orchestration-alignment-design.md` 顶部状态行追加实施记录(仿照
op-block-exec-scheduler-unification-design.md 的"实施记录"节):实施日期、提交链
(Task 1→5 的 commit hash)、验证结果(全绿清单)。

- [ ] **Step 5: Commit**

```bash
git add docs/2026-08-11-op-driver-orchestration-alignment-design.md
git commit --no-verify -m "docs(engine): record OP driver orchestration alignment implementation"
```

---

## 任务依赖与风险注记

- **依赖**:Task 1 → 2 → 3 → 4;Task 5 收尾。Task 2 的 OpExecuteBlockResult 移动是 Task 3 的前提
  (OpBlockRegister.h 不能反向 include OpSchedulerImpl.h)。
- **每任务提交后保持构建 + 测试全绿**(强制约束):Task 3 把 5 处实例化点(4 测试 + Initializer)
  一并更新,正是为了 ctor 改动不破坏任何 target。
- **风险最高点**:Task 4 的三阶段改写(e2e 80 例即闸)。registerOpBlock 数据来源映射已在 Task 2
  单测钉死,engine 侧只删不写。
- **前向依赖(记入决策)**:概念形式 executeBlock 也被通用 buildPayload 引用(L1426,Transaction
  范围);OP 组合根下该调用在 `if constexpr (!c_opMode)` 内丢弃,不实例化、不冲突。本期不做
  OP 化块构建。
- **已知不单测**:opstackRegisterBlock 的 writeOne 抛错传播路径(需自定义 throwing storage2
  backend,成本过高),由 engine 屏障既有 -32603 通道语义覆盖,代码审查确认不变。
