#pragma once

#include "SchedulerBaseImpl.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <queue>

namespace bcos::transaction_scheduler
{

#define BASELINE_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

template <class SchedulerImpl, protocol::IsBlockHeaderFactory BlockHeaderFactory,
    concepts::ledger::IsLedger Ledger>
class BaselineScheduler : public scheduler::SchedulerInterface
{
private:
    SchedulerImpl& m_schedulerImpl;
    BlockHeaderFactory& m_blockHeaderFactory;
    Ledger& m_ledger;
    crypto::Hash const& m_hashImpl;

    int64_t m_lastExecutedBlockNumber = -1;
    std::mutex m_executeMutex;

    int64_t m_lastcommittedBlockNumber = -1;
    std::mutex m_commitMutex;

    struct ExecuteResult
    {
        protocol::Block::Ptr m_block;
    };
    std::queue<ExecuteResult> m_results;
    std::mutex m_resultsMutex;

public:
    BaselineScheduler(SchedulerImpl& schedulerImpl, BlockHeaderFactory& blockFactory,
        Ledger& ledger, crypto::Hash const& hashImpl)
      : m_schedulerImpl(schedulerImpl),
        m_blockHeaderFactory(blockFactory),
        m_ledger(ledger),
        m_hashImpl(hashImpl)
    {}
    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) noexcept = default;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) noexcept = default;
    ~BaselineScheduler() override = default;

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool sysBlock)>
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
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                        nullptr, false);

                    co_return;
                }

                auto blockHeader = block->blockHeaderConst();
                if (self->m_lastExecutedBlockNumber != -1 &&
                    blockHeader->number() - self->m_lastExecutedBlockNumber != 1)
                {
                    auto message =
                        fmt::format("Discontinuous execute block number! expect: {} input: {}",
                            self->m_lastExecutedBlockNumber + 1, blockHeader->number());

                    executeLock.unlock();
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 scheduler::SchedulerError::InvalidBlockNumber, message),
                        nullptr, false);
                    co_return;
                }

                self->m_lastExecutedBlockNumber = blockHeader->number();
                self->m_schedulerImpl.start();
                auto blockHeaderPtr = block->blockHeaderConst();
                auto transactions =
                    RANGES::iota_view<uint64_t, uint64_t>(0LU, block->transactionsSize()) |
                    RANGES::views::transform(
                        [&block](uint64_t index) { return block->transaction(index); }) |
                    RANGES::to<std::vector<protocol::Transaction::ConstPtr>>();
                auto receipts = co_await self->m_schedulerImpl.execute(
                    *blockHeaderPtr, transactions | RANGES::views::transform([
                    ](protocol::Transaction::ConstPtr const& transactionPtr) -> auto& {
                        return *transactionPtr;
                    }));
                auto stateRoot =
                    co_await self->m_schedulerImpl.finish(*blockHeaderPtr, self->m_hashImpl);

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

                // TODO: 交易和回执root跟交易顺序有关系，分片后需要组合顺序再计算
                newBlockHeader->setTxsRoot(block->calculateTransactionRoot(self->m_hashImpl));
                newBlockHeader->setReceiptsRoot(block->calculateReceiptRoot(self->m_hashImpl));
                newBlockHeader->calculateHash(self->m_hashImpl);

                std::unique_lock resultsLock(self->m_resultsMutex);
                self->m_results.push({.m_block = std::move(block)});
                resultsLock.unlock();
                executeLock.unlock();

                callback(nullptr, std::move(newBlockHeader), false);
                co_return;
            }
            catch (bcos::Error& e)
            {}
        }(this, std::move(block), std::move(callback)));
    }

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::BlockHeader::Ptr blockHeader,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                std::unique_lock commitLock(self->m_commitMutex, std::try_to_lock);
                if (!commitLock.owns_lock())
                {
                    auto message = fmt::format(
                        "Another block:{} is committing!", self->m_lastcommittedBlockNumber);
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                        nullptr);

                    co_return;
                }

                if (self->m_lastcommittedBlockNumber != -1 &&
                    blockHeader->number() - self->m_lastcommittedBlockNumber != 1)
                {
                    auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                        blockHeader->number(), self->m_lastcommittedBlockNumber + 1);

                    commitLock.unlock();
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 scheduler::SchedulerError::InvalidBlockNumber, message),
                        nullptr);
                    co_return;
                }

                auto& result = self->m_results.back();

                // Write block and receipt
                co_await self->m_ledger.template setBlock<concepts::ledger::HEADER,
                    concepts::ledger::TRANSACTIONS_METADATA, concepts::ledger::RECEIPTS,
                    concepts::ledger::NONCES>(*(result.m_block));

                // Write status
                co_await self->m_schedulerImpl.commit();

                commitLock.unlock();
                callback(nullptr,
                    std::make_shared<ledger::LedgerConfig>(co_await self->m_ledger.getConfig()));
                co_return;
            }
            catch (bcos::Error& e)
            {}
        }(this, std::move(header), std::move(callback)));
    }

    void status(
        [[maybe_unused]] std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)>
            callback) override
    {}

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                       decltype(callback) callback) -> task::Task<void> {
            auto blockHeader = self->m_blockHeaderFactory.createBlockHeader();

            auto receipt = co_await self->m_schedulerImpl.call(*blockHeader, *transaction);

            callback(nullptr, std::move(receipt));
        }(this, std::move(transaction), std::move(callback)));
    }

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
    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {}

    void stop() override{};
};

}  // namespace bcos::transaction_scheduler