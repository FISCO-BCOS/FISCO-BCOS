#pragma once
// OP/ethereum 统一编排骨架（继承式 Template Method，CRTP 非虚分派，spec v3）。
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/exception/diagnostic_information.hpp>
#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>  // v3：std::apply 需要
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{
/// 执行结果载荷（v3 B2 + P1-2 修订）：receipts + 模式特定扩展 + 提交侧富信息。
/// modeExtra 用 shared_ptr<void> 保持基类 OP-free（A3）——OP 填 OpExecuteBlockResult。
/// m_transactions/m_sysBlock/m_mptDelta 服务 ethereum 的 prewriteBlockToBuffer + notifier
/// （v3 P1-2：ethereum 富结果不能只靠 receipts+modeExtra）。
struct SchedulerExecuteResult
{
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    std::shared_ptr<void> modeExtra;
    protocol::ConstTransactionsPtr m_transactions;  // ethereum（OP 恒空）
    bool m_sysBlock{false};
    std::shared_ptr<void> m_mptDelta;  // ethereum MPT delta（OP 恒空）
};

template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger, class Derived>
class SchedulerSkeleton : public scheduler::SchedulerInterface
{
public:
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

    // v3（P1-7 补）：status/reset/preExecuteBlock 骨架默认实现（ethereum/OP 同语义，
    // 参照旧 OpBlockScheduler:220-235 的 trivial 实现）——SchedulerInterface
    // 纯虚，缺则派生无法实例化。
    void status(std::function<void(Error::Ptr, protocol::Session::ConstPtr)>) override
    { /* no-op */
    }
    void reset(std::function<void(Error::Ptr)>) override
    { /* no-op */
    }
    void preExecuteBlock(protocol::Block::Ptr, bool, std::function<void(Error::Ptr)>) override
    { /* no-op */
    }

protected:
    // CRTP 非虚分派（A4）
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }

    // 共享机制（A7：无锁 fast-path 缓存命中保留）——v3（P0-3）：真实 fast-path 是 m_results deque +
    // m_resultsMutex（BaselineScheduler.h:397-412），非 ExecuteResultCache 类型。
    // fastPathHit 的签名在 Task 3 迁入 m_results 后按实际形态定义；smoke 测试用 FakeDerived 的
    // m_results 直接验证「缓存命中不重执行」。
    // ... 从 BaselineScheduler 抽取的 forkView/tryExecuteLock/continuityCheck/backpressureOk/
    // pushResult/popResult/setBlockNumberNotifier/notifyBlockNumber 具名方法（Task 3 迁入）

    // CRTP hooks（B1：具体类型，无 auto 形参）
    // v3（P0-5）：getTransactions 是协程（现自由函数 BaselineScheduler.cpp:3-17 返回
    // task::Task<vector<ConstPtr>>）——hook 返回协程类型，coExecuteBlock 步骤② co_await。
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block&, typename MultiLayerStorage::ViewType&);
    task::Task<SchedulerExecuteResult> execute(typename MultiLayerStorage::ViewType&,
        protocol::BlockHeader const&, std::vector<protocol::Transaction::ConstPtr> const&,
        ledger::LedgerConfig const&);
    task::Task<protocol::BlockHeader::Ptr> finishExecute(typename MultiLayerStorage::ViewType&,
        SchedulerExecuteResult const&, protocol::BlockHeader const&, protocol::Block&,
        std::vector<protocol::Transaction::ConstPtr> const&, ledger::LedgerConfig const&,
        bool& sysBlock);
    void verifyResult(protocol::BlockHeader::Ptr executed, protocol::BlockHeader const& announced);
    // v3（P1-3）：commit 返回 shared_ptr<MutableStorage>（mergeBackStorage 收 MutableStorage 引用，
    // 非 shared_ptr<MultiLayerStorage>）；v3（P1-4）：rawTxBytes 经 modeExtra 或额外参带过（Task 4
    // pin）。
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        typename MultiLayerStorage::ViewType&, protocol::BlockHeader::Ptr,
        SchedulerExecuteResult const&);

    // A3：异常分类钩子（基类不命名 OP 类型）——v3（P0-4）：SchedulerError 是普通 enum，
    // 直接返回 `scheduler::SchedulerError`，无 ErrorEnum 嵌套。
    virtual scheduler::SchedulerError classifyException(std::exception_ptr) const = 0;

    // 模板方法（共享流程，B4：execute → finish → verify；coExecuteBlock ①-⑥）
    task::Task<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        protocol::Block::Ptr block, bool verify)
    {
        try
        {
            auto blockHeader = block->blockHeader();
            auto number = blockHeader->number();

            // ① 无锁 fast-path（A7）：m_results 缓存命中直接回，不取 m_executeMutex。
            //    fastPathHit 的实际形态在 Task 3 迁入 m_results 后定义（v3 P0-3）。
            if (auto cached = derived().fastPathHit(number))
            {
                co_return {nullptr, cached->first, cached->second};
            }

            // ① execute 锁（FIB-102）。
            if (!derived().tryExecuteLock())
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                               "Another block is executing!"),
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
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks,
                               "Not found transactions in txpool for block!"),
                    nullptr, false};
            }

            // cfg 在 getTransactions 之后加载（v3 保持现状顺序，BaselineScheduler.h:472-473）。
            auto ledgerConfig = derived().loadLedgerConfig(view, number);

            // ③ hook ②：执行内核。
            auto executeResult =
                co_await derived().execute(view, *blockHeader, transactions, *ledgerConfig);

            // ④ hook ③：finishExecute（executed 头在此算出——stateRoot/hash，verify 在其后）。
            bool sysBlock = false;
            auto executedHeader = co_await derived().finishExecute(
                view, executeResult, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // ⑤ hook ④：verify。v3（P1-1）verify 错误码保真（ethereum InvalidBlocks 不丢）随
            // Task 3 的 verify hook 签名修订落地（task::Task<Error::Ptr>）；Task 2 保持 brief 的
            // void 契约——verify 语义（门控/无条件）由派生 hook 各自承载（B4/H3）。
            derived().verifyResult(executedHeader, *blockHeader);

            // ⑥ push + 推进（FIB-103/FIB-104 顺序：先 push 结果，成功后推进 counter）。
            derived().pushResult(number, block, executedHeader, std::move(executeResult), sysBlock);
            derived().updateLastExecutedBlockNumber(number);

            co_return {nullptr, executedHeader, sysBlock};
        }
        catch (std::exception& e)
        {
            auto message =
                fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
    }

    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        protocol::BlockHeader::Ptr header)
    {
        try
        {
            auto number = header->number();

            // ① commit 锁（FIB-101）。
            if (!derived().tryCommitLock())
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                               "Another block is committing!"),
                    nullptr};
            }

            // ② 连续性（bootstrap + already-committed + discontinuous，FIB-101）。
            if (!derived().commitContinuityCheck(number))
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
            auto ledgerConfig = derived().loadCommitLedgerConfig(header);
            derived().updateLastCommittedBlockNumber(number);
            derived().notifyBlockNumber(number);

            co_return {nullptr, ledgerConfig};
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
    }
};
}  // namespace bcos::scheduler_v1
