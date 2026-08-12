#pragma once
// OP/ethereum 统一编排骨架（继承式 Template Method，CRTP 非虚分派，spec v3）。
//
// Task 3a/3b：共享编排机制（锁/连续性/背压/push/pop/notify/fork/无锁 fast-path）与
// 其状态（m_results + m_resultsMutex / m_executeMutex + m_lastExecutedBlockNumber /
// m_commitMutex + m_lastCommittedBlockNumber / m_multiLayerStorage / m_blockFactory /
// m_ledger / notifier / m_asyncGroup / m_transactionSubmitResultFactory /
// m_mptCommitObserver）迁入本基类；coExecuteBlock/coCommitBlock 为共享流程（锁直接
// 持有跨整个 execute/commit 块，FIB-101/102 不变式保留），5 个 CRTP hook 由派生实现。
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ITTAPI.h>
#include <fmt/format.h>
#include <oneapi/tbb/task_group.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/zip.hpp>
#include <tuple>  // v3：std::apply 需要
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{
#define BASELINE_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

/// 执行结果载荷（v3 B2 + P1-2 修订 + Task 3b concern 2 修订）：receipts + 模式特定扩展 +
/// 提交侧富信息。modeExtra 用 shared_ptr<void> 保持基类 OP-free（A3）——OP 填 OpExecuteBlockResult。
/// m_transactions/m_sysBlock/m_mptDelta 服务 ethereum 的 prewriteBlockToBuffer + notifier
/// （v3 P1-2：ethereum 富结果不能只靠 receipts+modeExtra）。Task 3b（concern 2 + fast-path）：
/// 增 m_block/m_executedBlockHeader 通道——ethereum commit hook 用 m_block
/// （setBlockHeader/setLogsBloom），fastPathHit / commit 号码复核用 m_executedBlockHeader。
struct SchedulerExecuteResult
{
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    std::shared_ptr<void> modeExtra;                // OP：OpExecuteBlockResult（OP 恒空）
    protocol::ConstTransactionsPtr m_transactions;  // ethereum（OP 恒空）
    bool m_sysBlock{false};
    std::optional<ledger::mpt::MPTDeltaLayer> m_mptDelta;  // ethereum MPT delta（OP 恒空）
    protocol::Block::Ptr m_block;  // ethereum commit hook：setBlockHeader/setLogsBloom
    protocol::BlockHeader::Ptr m_executedBlockHeader;  // fast-path / commit 号码复核
};

/// 返回当前时间（毫秒，自 epoch 起）。与 BaselineScheduler.h 的自由函数同签名；
/// 骨架 coExecuteBlock/coCommitBlock 的日志用。定义在 BaselineScheduler.cpp
/// （transaction-scheduler 库），仅声明供模板方法按需实例化。
std::chrono::milliseconds::rep current();

template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger, class Derived>
class SchedulerSkeleton : public scheduler::SchedulerInterface
{
public:
    // tbb::task_group 的析构是潜在抛异常（C++17 前）→ 显式 noexcept 保覆盖契约；
    // 等 m_asyncGroup 排空（与旧 BaselineScheduler 析构同语义）。
    ~SchedulerSkeleton() noexcept { m_asyncGroup.wait(); }

    void executeBlock(protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr, protocol::BlockHeader::Ptr, bool)> callback) override
    {
        task::wait([this, block = std::move(block), verify,
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            std::apply(cb, co_await coExecuteBlock(std::move(block), verify));
        }());
    }

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr)> callback) override
    {
        task::wait([this, header = std::move(header),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            std::apply(cb, co_await coCommitBlock(std::move(header)));
        }());
    }

    // v3（B3）：call/getCode/getABI/getPendingStorageAt 纯虚——派生各实现。
    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) override = 0;
    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)>) override = 0;
    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)>) override = 0;
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, protocol::BlockNumber) override = 0;

    // v3（P1-7）：status/reset/preExecuteBlock 骨架默认实现（ethereum/OP 同语义，参照旧
    // OpBlockScheduler:220-235 的 trivial 实现——回调以成功收尾，不得悬挂调用方）。
    void status(std::function<void(Error::Ptr, protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }
    void reset(std::function<void(Error::Ptr)> callback) override { callback(nullptr); }
    void preExecuteBlock(
        protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

protected:
    // 默认可构造（FakeDerived 等派生 stub 用；真实状态留空）。派生构造器可显式接线。
    SchedulerSkeleton() = default;
    // 共享编排机制状态接线（Task 3b：BaselineScheduler 构造器调用；OP 用 wire stub）。
    SchedulerSkeleton(MultiLayerStorage& multiLayerStorage, protocol::BlockFactory& blockFactory,
        Ledger& ledger, protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory)
      : m_multiLayerStorage(&multiLayerStorage),
        m_blockFactory(&blockFactory),
        m_ledger(&ledger),
        m_transactionSubmitResultFactory(&transactionSubmitResultFactory)
    {}

    // CRTP 非虚分派（A4）
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }

    // ================================================================
    // 共享编排机制状态（Task 3a 迁入；派生构造器接线）。原始指针/值成员保证默认可构造
    // （skeleton 模板方法按需实例化，派生 stub 可遮蔽；真实状态由派生构造器 set）。
    // ================================================================
    MultiLayerStorage* m_multiLayerStorage = nullptr;
    protocol::BlockFactory* m_blockFactory = nullptr;
    Ledger* m_ledger = nullptr;
    protocol::TransactionSubmitResultFactory* m_transactionSubmitResultFactory = nullptr;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_transactionNotifier;

    // FIB-101 / FIB-102：每个 counter 由且仅由一个 mutex 拥有，仅在持锁时读写。普通
    // int64_t 足够，无需原子量。m_lastExecutedBlockNumber 归 m_executeMutex；
    // m_lastCommittedBlockNumber 归 m_commitMutex。
    int64_t m_lastExecutedBlockNumber{-1};
    std::mutex m_executeMutex;
    int64_t m_lastCommittedBlockNumber{-1};
    std::mutex m_commitMutex;
    tbb::task_group m_asyncGroup;

    // 无锁 fast-path 缓存（A7 / v3 P0-3：m_results deque + m_resultsMutex 实际形态）。
    static constexpr size_t MAX_PENDING_RESULTS = 16;
    std::deque<std::shared_ptr<SchedulerExecuteResult>> m_results;
    std::mutex m_resultsMutex;

    /// Post-commit hook over each MPT block's node delta —— 路径修剪 seam（CommitObserver.h）。
    /// 默认 no-op observer；wiring 期 setMPTCommitObserver 替换。提交路径读。
    std::shared_ptr<ledger::mpt::CommitObserver> m_mptCommitObserver =
        std::make_shared<ledger::mpt::NoopCommitObserver>();

    // ================================================================
    // 共享编排机制（Task 3a：从 BaselineScheduler coExecuteBlock/coCommitBlock 内联逻辑抽取）。
    // CRTP：经 derived().X() 解析到本类（派生 stub 可遮蔽）。签名以 FakeDerived 契约 +
    // BaselineScheduler 实际形态对齐。
    // ================================================================

    // 无锁 fast-path（A7）：m_results 缓存命中直接回，不取 m_executeMutex。front=最新。
    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number)
    {
        std::unique_lock resultsLock(m_resultsMutex);
        if (!m_results.empty())
        {
            auto frontNumber = m_results.front()->m_executedBlockHeader->number();
            auto backNumber = m_results.back()->m_executedBlockHeader->number();
            if (number <= frontNumber && number >= backNumber)
            {
                BASELINE_SCHEDULER_LOG(INFO) << "Block has been executed, return result directly";
                auto& result = m_results.at(frontNumber - number);
                return std::pair{result->m_executedBlockHeader, result->m_sysBlock};
            }
        }
        return std::nullopt;
    }

    // execute 写落在 view 的 mutable 层；commit 的 mergeBackStorage 持久化。
    typename MultiLayerStorage::ViewType forkView() { return m_multiLayerStorage->fork(); }

    // FIB-102：连续性复查（在 m_executeMutex 持有期间调用，见 coExecuteBlock）。
    bool continuityCheck(protocol::BlockNumber number)
    {
        if (m_lastExecutedBlockNumber != -1 && number - m_lastExecutedBlockNumber != 1)
        {
            auto message = fmt::format("Discontinuous execute block number! expect: {} input: {}",
                m_lastExecutedBlockNumber + 1, number);
            BASELINE_SCHEDULER_LOG(INFO) << message;
            return false;
        }
        return true;
    }

    // FIB-103：背压——pending-result 队列达容量时拒绝新执行。
    bool backpressureOk()
    {
        std::unique_lock resultsLock(m_resultsMutex);
        if (m_results.size() >= MAX_PENDING_RESULTS)
        {
            auto message = fmt::format("Too many pending execution results: {}", m_results.size());
            BASELINE_SCHEDULER_LOG(WARNING) << message;
            return false;
        }
        return true;
    }

    // FIB-103 / FIB-104：严格顺序 push——先 pushView（回滚目标）再 push_result；若
    // push 抛则 popFrontStorage 回滚。view 与 result 共享 m_resultsMutex 临界区
    // （与 backpressureOk 同锁，m_executeMutex 防 execute 间插入，commit 只缩队，故
    // 此处 size 不会超 MAX_PENDING_RESULTS）。
    void pushResult(protocol::BlockNumber number, protocol::Block::Ptr block,
        protocol::BlockHeader::Ptr executedHeader, SchedulerExecuteResult result, bool sysBlock,
        typename MultiLayerStorage::ViewType view)
    {
        (void)number;
        std::unique_lock resultsLock(m_resultsMutex);
        assert(m_results.size() < MAX_PENDING_RESULTS);

        result.m_block = std::move(block);
        result.m_executedBlockHeader = std::move(executedHeader);
        result.m_sysBlock = sysBlock;

        m_multiLayerStorage->pushView(std::move(view));
        try
        {
            m_results.push_front(std::make_shared<SchedulerExecuteResult>(std::move(result)));
        }
        catch (...)
        {
            m_multiLayerStorage->popFrontStorage();
            throw;
        }
    }

    // peek（不 pop）：返回最旧 pending result；空队 → nullptr（骨架 coCommitBlock 报
    // UnknownError "Unexpected empty results!"）。号码复核在骨架 coCommitBlock（保真
    // InvalidBlockNumber "Commit block does not match pending execution result"）。
    std::shared_ptr<SchedulerExecuteResult> peekResult(protocol::BlockNumber /*number*/)
    {
        std::unique_lock resultsLock(m_resultsMutex);
        if (m_results.empty())
        {
            return nullptr;
        }
        return m_results.back();
    }

    // pop（M-1 guard：merge 成功之后、回调/notifier 之前；pending result 被并发改动则抛）。
    void popResult(protocol::BlockNumber number)
    {
        std::unique_lock resultsLock(m_resultsMutex);
        if (m_results.empty() || m_results.back()->m_executedBlockHeader->number() != number)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("Pending execution result changed during commit!"));
        }
        m_results.pop_back();
    }

    // FIB-101：commit 连续性（bootstrap + already-committed + discontinuous）。block 0
    // （isSysContractDeploy）跳过 bootstrap-anchored 校验（genesis 系统合约部署块，节点启动
    // initSysContract 时恰好一次；m_lastCommittedBlockNumber 在 merge 成功后仍推进到 0）。
    task::Task<bool> commitContinuityCheck(protocol::BlockNumber number)
    {
        if (!isSysContractDeploy(number))
        {
            if (m_lastCommittedBlockNumber == -1)
            {
                m_lastCommittedBlockNumber = co_await ledger::getCurrentBlockNumber(*m_ledger);
            }
            if (m_lastCommittedBlockNumber != -1 && number <= m_lastCommittedBlockNumber)
            {
                auto message = fmt::format(
                    "Block already committed: {}! latest: {}", number, m_lastCommittedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return false;
            }
            if (m_lastCommittedBlockNumber != -1 && number - m_lastCommittedBlockNumber != 1)
            {
                auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                    number, m_lastCommittedBlockNumber + 1);
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return false;
            }
        }
        co_return true;
    }

    // FIB-104：骨架唯一一次 merge（原子：要么全可见要么全不可见）。重 IO（RocksDB/cache）；
    // 不持 m_resultsMutex（防 execute 与 commit IO 序列化）。pop 在 merge 成功之后。
    task::Task<void> mergeBackStorage(typename MultiLayerStorage::MutableStorage& storage)
    {
        ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().MERGE_STATE);
        co_await m_multiLayerStorage->mergeBackStorage(storage);
    }

    // P1-5：post-merge CommitObserver——「merge 后、pop 前」时序（CommitObserver.h 契约）。
    // 仅 MPT 块触发（ethereum 填 m_mptDelta；OP 恒空 → no-op）。
    void postMergeCommitObserver(protocol::BlockNumber number, SchedulerExecuteResult const& result)
    {
        if (result.m_mptDelta)
        {
            m_mptCommitObserver->onCommit(number, *result.m_mptDelta);
        }
    }

    // cfg 在 getTransactions 之后加载（v3 保持现状顺序，BaselineScheduler.h:472-473）。
    task::Task<ledger::LedgerConfig::Ptr> loadLedgerConfig(
        typename MultiLayerStorage::ViewType& view, protocol::BlockNumber number)
    {
        co_return co_await ledger::getLedgerConfig(view, number, *m_blockFactory);
    }

    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = co_await ledger::getLedgerConfig(*m_ledger);
        ledgerConfig->setHash(header->hash());
        co_return ledgerConfig;
    }

    void updateLastExecutedBlockNumber(protocol::BlockNumber number)
    {
        m_lastExecutedBlockNumber = number;
    }

    void updateLastCommittedBlockNumber(protocol::BlockNumber number)
    {
        m_lastCommittedBlockNumber = number;
    }

    // M-2：异步 tx-submit 结果构造 + 块号/tx notifier（m_asyncGroup 上跑；commit 锁已释放）。
    void notifyBlockNumber(protocol::BlockNumber number,
        std::shared_ptr<SchedulerExecuteResult> result, ledger::LedgerConfig const& ledgerConfig)
    {
        m_asyncGroup.run([this, result = std::move(result), blockHash = ledgerConfig.hash(),
                             blockNumber = ledgerConfig.blockNumber()]() {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

            auto submitResults =
                ::ranges::views::zip(
                    ::ranges::views::iota(0), *result->m_transactions, result->receipts) |
                ::ranges::views::transform(
                    [&](auto input) -> protocol::TransactionSubmitResult::Ptr {
                        auto&& [index, transaction, receipt] = input;

                        auto submitResult =
                            m_transactionSubmitResultFactory->createTxSubmitResult();
                        submitResult->setStatus(receipt->status());
                        submitResult->setTxHash(transaction->hash());
                        submitResult->setBlockHash(blockHash);
                        submitResult->setTransactionIndex(static_cast<int64_t>(index));
                        submitResult->setNonce(std::string(transaction->nonce()));
                        submitResult->setTransactionReceipt(receipt);
                        submitResult->setSender(std::string(transaction->sender()));
                        submitResult->setTo(std::string(transaction->to()));
                        submitResult->setType(transaction->type());

                        return submitResult;
                    }) |
                ::ranges::to<std::vector>();

            auto submitResultsPtr = std::make_shared<bcos::protocol::TransactionSubmitResults>(
                std::move(submitResults));
            m_blockNumberNotifier(blockNumber);
            m_transactionNotifier(
                blockNumber, std::move(submitResultsPtr), [](const Error::Ptr& error) {
                    if (error)
                    {
                        BASELINE_SCHEDULER_LOG(WARNING)
                            << "Push block notify error!" << boost::diagnostic_information(*error);
                    }
                });
        });
    }

    // notifier / observer 接线（wiring 期调用，块流程开始前）。
    void setBlockNumberNotifier(std::function<void(bcos::protocol::BlockNumber)> notifier)
    {
        m_blockNumberNotifier = std::move(notifier);
    }
    void setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            notifier)
    {
        m_transactionNotifier = std::move(notifier);
    }
    void setMPTCommitObserver(std::shared_ptr<ledger::mpt::CommitObserver> observer)
    {
        if (observer)
        {
            m_mptCommitObserver = std::move(observer);
        }
    }

    // ================================================================
    // CRTP hooks（B1：具体类型，无 auto 形参）。派生各实现（派生定义遮蔽本声明，经
    // derived() 非虚分派）。仅声明不定义——派生必须提供全部 5 个。
    // ================================================================
    // ① 交易来源（v3 P0-5：协程，coExecuteBlock 步骤② co_await）。
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block&, typename MultiLayerStorage::ViewType&);
    // ② 执行内核 → 包 SchedulerExecuteResult 富结果。
    task::Task<SchedulerExecuteResult> execute(typename MultiLayerStorage::ViewType&,
        protocol::BlockHeader const&, std::vector<protocol::Transaction::ConstPtr> const&,
        ledger::LedgerConfig const&);
    // ③ finish（MPT 前置 + populateBlockHeader + calculateHash；先 finish 后 verify）。
    //    Task 3b：SchedulerExecuteResult& 非 const——hook 把 m_mptDelta 写回载荷。
    task::Task<protocol::BlockHeader::Ptr> finishExecute(typename MultiLayerStorage::ViewType&,
        SchedulerExecuteResult&, protocol::BlockHeader const&, protocol::Block&,
        std::vector<protocol::Transaction::ConstPtr> const&, ledger::LedgerConfig const&,
        bool& sysBlock);
    // ④ verify（P1-1：返回 task::Task<Error::Ptr>，null=通过；非 null=保真错误码。
    //    Task 3b（I-2）：bool verify 穿入——ethereum hook 内 if(verify) hash 对比）。
    task::Task<Error::Ptr> verifyResult(
        protocol::BlockHeader::Ptr executed, protocol::BlockHeader const& announced, bool verify);
    // ⑤ commit → 返回可 merge 的 storage（v3 P1-3：prewriteStorage，非 execute view）。
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        typename MultiLayerStorage::ViewType&, protocol::BlockHeader::Ptr,
        SchedulerExecuteResult const&);

    // A3：异常分类钩子（基类不命名 OP 类型）。
    virtual scheduler::SchedulerError classifyException(std::exception_ptr) const = 0;

    /// 异常消息钩子（wiring Task 5b）：`catch(...)` 兜底路径（OP 的 RTTI 旁路异常逃过
    /// catch(std::exception&)）用 `derived().describeException(current_exception())` 构造错误
    /// 消息——默认返回通用占位串（与旧行为逐字相同）；派生（OpScheduler）经 rethrow + typed
    /// catch（FISCO 类型 typeinfo 稳定，可靠绑定）恢复原始 OpConsensusError/OpStorageError 的
    /// what()，否则 catch(...) 丢消息（"-fno-rtti evmone 边界的 std::exception typeinfo 非唯一，
    /// catch(const std::exception&) 不可靠绑定"）。签名 OP-free（返回 std::string），A3 不变。
    virtual std::string describeException(std::exception_ptr) const
    {
        return "unclassified exception, RTTI typed-catch bypassed";
    }

    // ================================================================
    // 模板方法（共享流程，B4：execute → finish → verify；coExecuteBlock ①-⑥）
    // ================================================================
    task::Task<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        protocol::Block::Ptr block, bool verify)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_BLOCK);
        try
        {
            auto blockHeader = block->blockHeader();
            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                << block->transactionsMetaDataSize() << " | " << block->transactionsSize();
            auto number = blockHeader->number();

            // ① 无锁 fast-path（A7）：缓存命中直接回，不取 m_executeMutex。
            if (auto cached = derived().fastPathHit(number))
            {
                co_return {nullptr, cached->first, cached->second};
            }

            // ① execute 锁（FIB-102）：持有至 try 块结束（锁内复查 + counter 写都在锁内）。
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = std::string{"Another block is executing!"};
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }

            // ① 连续性（FIB-102，锁内复查）。
            if (!derived().continuityCheck(number))
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                               "Discontinuous execute block number!"),
                    nullptr, false};
            }

            // ① 背压（FIB-103）。
            if (!derived().backpressureOk())
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                               "Too many pending execution results!"),
                    nullptr, false};
            }

            // ① fork view（execute 写落在 mutable 层，commit 的 mergeBackStorage 持久化）。
            auto view = derived().forkView();
            view.newMutable();

            // ② hook ①：交易来源（v3 P0-5：协程）。
            auto transactions = co_await derived().getTransactions(*block, view);
            if (std::any_of(transactions.begin(), transactions.end(),
                    [](auto const& tx) { return tx == nullptr; }))
            {
                auto message =
                    fmt::format("Not found transactions in txpool for block: {}", number);
                BASELINE_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false};
            }

            // cfg 在 getTransactions 之后加载（v3 保持现状顺序，BaselineScheduler.h:472-473）。
            auto ledgerConfig = co_await derived().loadLedgerConfig(view, number);

            // ③ hook ②：执行内核。
            auto executeResult =
                co_await derived().execute(view, *blockHeader, transactions, *ledgerConfig);

            // ④ hook ③：finishExecute（executed 头在此算出——stateRoot/hash，verify 在其后）。
            bool sysBlock = false;
            auto executedHeader = co_await derived().finishExecute(
                view, executeResult, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // ⑤ hook ④：verify（P1-1：Error::Ptr 保真 InvalidBlocks，不经 classify）。
            auto verifyError =
                co_await derived().verifyResult(executedHeader, *blockHeader, verify);
            if (verifyError)
            {
                co_return {verifyError, nullptr, false};
            }

            // ⑥ push + 推进（FIB-103/FIB-104 顺序：先 push 结果，成功后推进 counter）。
            derived().pushResult(
                number, block, executedHeader, std::move(executeResult), sysBlock, std::move(view));
            derived().updateLastExecutedBlockNumber(number);

            // （旧 BaselineScheduler 的 "Execute block finished" 诊断日志随 execute
            // 拆 hook 移除——它依赖 executedHeader->hash()/stateRoot()，是 ethereum
            // 特有遥测；ittapi EXECUTE_BLOCK report 保留在骨架。）
            co_return {nullptr, std::move(executedHeader), sysBlock};
        }
        catch (std::exception& e)
        {
            auto message =
                fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
        catch (...)
        {
            // RTTI-bypass fallback（OpScheduler 接线 Task 4 发现）：-fno-rtti 的 libevmone.a 带
            // 非唯一 typeinfo，runtime_error 子类（OpSchedulerImpl 的
            // OpConsensusError/OpStorageError） 会逃出上面的
            // catch(std::exception&)（Storage2State.h:195-199 实测同现象）。没有此兜底， OP
            // 异常会绕过 classifyException 直接逃出 executeBlock。classifyException 的 rethrow +
            // typed catch 对已归一（execute hook 内 catch(...) 重抛）的 FISCO 类型可靠绑定。
            auto message = std::string{"Execute block failed! ("} +
                           derived().describeException(std::current_exception()) + ")";
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
    }

    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        protocol::BlockHeader::Ptr header)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().COMMIT_BLOCK);
        try
        {
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block: " << header->number();
            auto number = header->number();
            auto now = current();

            // ① commit 锁（FIB-101）：持有至异步 notifier 前（counter 读写都在锁内）。
            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }

            // ② 连续性（bootstrap + already-committed + discontinuous，FIB-101）。
            if (!co_await derived().commitContinuityCheck(number))
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                               "Commit block continuity check failed!"),
                    nullptr};
            }

            // ③ peek（不 pop）。
            auto result = derived().peekResult(number);
            if (!result)
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError,
                               "Unexpected empty results!"),
                    nullptr};
            }
            // 保真 InvalidBlockNumber（与现状 BaselineScheduler.h:702-713 一致）。
            if (result->m_executedBlockHeader->number() != number)
            {
                auto message = fmt::format(
                    "Commit block does not match pending execution result: input: {} pending: {}",
                    number, result->m_executedBlockHeader->number());
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }

            // ④ hook ⑤：commit → 返回可 merge 的 storage（v3 P1-3：MutableStorage）。
            auto view = derived().forkView();
            auto storage = co_await derived().commit(view, header, *result);

            // ⑤ 骨架唯一一次 mergeBackStorage（A1/FIB-104，无双重 merge）。
            co_await derived().mergeBackStorage(*storage);

            // ⑥ post-merge CommitObserver（P1-5：ethereum MPT observer「merge 后、pop 前」，
            //    OP 不设）。
            derived().postMergeCommitObserver(number, *result);

            // ⑦ pop（定序在 merge 成功之后）。
            derived().popResult(number);

            // ⑧ 回调载荷 + notifier。
            auto ledgerConfig = co_await derived().loadCommitLedgerConfig(header);
            derived().updateLastCommittedBlockNumber(number);

            BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << number
                                         << " | elapsed: " << (current() - now) << "ms";
            commitLock.unlock();
            derived().notifyBlockNumber(number, std::move(result), *ledgerConfig);

            co_return {nullptr, ledgerConfig};
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
        catch (...)
        {
            // RTTI-bypass 兜底（同 coExecuteBlock 注释）：runtime_error 子类逃 typed catch，
            // 无此兜底会绕过 classifyException 逃出 commitBlock。
            auto message = std::string{"Commit block failed! ("} +
                           derived().describeException(std::current_exception()) + ")";
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
    }
};
}  // namespace bcos::scheduler_v1
