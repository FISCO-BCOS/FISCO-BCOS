#pragma once

#include "SchedulerBaseImpl.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <queue>

namespace bcos::transaction_scheduler
{

#define SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

template <class SchedulerImpl, protocol::IsBlockHeaderFactory BlockHeaderFactory>
class BaselineScheduler : public scheduler::SchedulerInterface
{
public:
    BaselineScheduler(SchedulerImpl&& schedulerImpl, BlockHeaderFactory&& blockHeaderFactory,
        crypto::Hash const& hashImpl)
      : m_schedulerImpl(std::forward<SchedulerImpl>(schedulerImpl)),
        m_blockHeaderFactory(std::forward<BlockHeaderFactory>(blockHeaderFactory)),
        m_hashImpl(hashImpl)
    {}
    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) noexcept = default;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) noexcept = default;
    ~BaselineScheduler() override = default;

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback) override
    {
        task::wait([](decltype(this) self, bcos::protocol::Block::Ptr block,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                std::unique_lock executeLock(self->m_executeMutex, std::try_to_lock);
                if (!executeLock.owns_lock())
                {
                    auto message = fmt::format(
                        "Another block:{} is executing!", self->m_lastExecutedBlockNumber);
                    SCHEDULER_LOG(INFO) << message;
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                        nullptr, false);

                    co_return;
                }


                auto blockHeader = block->blockHeaderConst();
                if (self->m_executingBlockNumber != 0 &&
                    blockHeader->number() - self->m_lastExecutedBlockNumber != 1)
                {
                    auto message =
                        fmt::format("Discontinuous input block number! expect: {} input: {}",
                            self->m_lastExecutedBlockNumber + 1, blockHeader->number());

                    executeLock.unlock();
                    SCHEDULER_LOG(INFO) << message;
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 scheduler::SchedulerError::InvalidBlockNumber, message),
                        nullptr, false);
                    co_return;
                }

                self->m_executingBlockNumber = blockHeader->number();
                self->m_schedulerImpl.start();
                auto blockHeaderPtr = block->blockHeaderConst();
                auto transactions =
                    RANGES::iota_view<uint64_t, uint64_t>(0LU, block->transactionsSize()) |
                    RANGES::views::transform(
                        [&block](uint64_t index) { return block->transaction(index); }) |
                    RANGES::to<std::vector<protocol::Transaction::ConstPtr>>();
                auto receipts = co_await self->m_schedulerImpl.execute(*blockHeaderPtr,
                    transactions | RANGES::views::transform(
                                       [](protocol::Transaction::ConstPtr const& transactionPtr) {
                                           return *transactionPtr;
                                       }));
                auto stateRoot = self->m_schedulerImpl.finish(*blockHeaderPtr, self->m_hashImpl);

                bcos::u256 totalGas = 0;
                for (auto& receipt : receipts)
                {
                    totalGas += receipt->gasUsed();
                    block->appendReceipt(std::move(receipt));
                }
                block->setBlockHeader(self->m_blockHeaderFactory.populateBlockHeader(blockHeader));

                auto newBlockHeader = block->blockHeader();
                newBlockHeader->setStateRoot(stateRoot);
                newBlockHeader->setGasUsed(totalGas);
                newBlockHeader->setTxsRoot(block->calculateTransactionRoot(self->m_hashImpl));
                newBlockHeader->setReceiptsRoot(block->calculateReceiptRoot(self->m_hashImpl));
                newBlockHeader->calculateHash(self->m_hashImpl);

                std::unique_lock queueLock(self->m_queueMutex);
                self->m_results->push({.m_block = std::move(block)});
                queueLock.unlock();
                executeLock.unlock();

                callback(nullptr, std::move(newBlockHeader), false);

                co_return;
            }
            catch (bcos::Error& e)
            {}
        }(this, std::move(block), std::move(callback)));
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override
    {}

    void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) override
    {}

    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)>) override
    {}

    void registerExecutor([[maybe_unused]] std::string name,
        [[maybe_unused]] bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {}

    void unregisterExecutor([[maybe_unused]] const std::string& name,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {}

    void reset([[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override {}

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {}

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {}

    // for performance, do the things before executing block in executor.
    void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr&&)> callback) override
    {}

    void stop() override{};

private:
    SchedulerImpl m_schedulerImpl;
    BlockHeaderFactory m_blockHeaderFactory;
    crypto::Hash const& m_hashImpl;

    int64_t m_lastExecutedBlockNumber = 0;
    std::mutex m_executeMutex;

    int64_t m_lastcommittedBlockNumber = 0;
    std::mutex m_commitMutex;

    struct ExecuteResult
    {
        protocol::Block::Ptr m_block;
    };
    std::queue<ExecuteResult> m_results;
    std::mutex m_resultsMutex;
};

}  // namespace bcos::transaction_scheduler