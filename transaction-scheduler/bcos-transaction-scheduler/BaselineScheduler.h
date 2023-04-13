#pragma once

#include "SchedulerBaseImpl.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-utilities/Common.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-task/TBBWait.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ITTAPI.h>
#include <fmt/format.h>
#include <ittnotify.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/task_group.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <type_traits>

namespace bcos::transaction_scheduler
{

#define BASELINE_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

// clang-format off
struct NotFoundTransactionError: public bcos::Error {};
// clang-format on

template <class SchedulerImpl, concepts::ledger::IsLedger Ledger>
class BaselineScheduler : public scheduler::SchedulerInterface
{
private:
    SchedulerImpl& m_schedulerImpl;
    protocol::BlockHeaderFactory& m_blockHeaderFactory;
    Ledger& m_ledger;
    txpool::TxPoolInterface& m_txpool;
    protocol::TransactionSubmitResultFactory& m_transactionSubmitResultFactory;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_transactionNotifier;
    crypto::Hash const& m_hashImpl;
    tbb::task_group m_asyncGroup;
    int64_t m_lastExecutedBlockNumber = -1;
    std::mutex m_executeMutex;
    int64_t m_lastcommittedBlockNumber = -1;
    std::mutex m_commitMutex;

    struct ExecuteResult
    {
        std::vector<protocol::Transaction::ConstPtr> m_transactions;
        protocol::Block::Ptr m_block;
    };
    std::list<ExecuteResult> m_results;
    std::mutex m_resultsMutex;

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block const& block) const
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().GET_TRANSACTIONS);
        if (block.transactionsSize() > 0)
        {
            co_return RANGES::views::iota(0LU, block.transactionsSize()) |
                RANGES::views::transform(
                    [&block](uint64_t index) { return block.transaction(index); }) |
                RANGES::to<std::vector<protocol::Transaction::ConstPtr>>();
        }

        co_return co_await m_txpool.getTransactions(
            RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
            RANGES::views::transform(
                [&block](uint64_t index) { return block.transactionHash(index); })) |
            RANGES::to<std::vector<protocol::Transaction::ConstPtr>>();
    }

    void finishExecute(RANGES::range auto const& receipts, protocol::BlockHeader const& blockHeader,
        protocol::BlockHeader& newBlockHeader, protocol::Block& block)
    {
        ittapi::Report finishReport(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().FINISH_EXECUTE);

        tbb::parallel_invoke(
            [&]() {
                bcos::u256 totalGas = 0;
                for (auto&& [receipt, index] :
                    RANGES::views::zip(receipts, RANGES::views::iota(0UL)))
                {
                    totalGas += receipt->gasUsed();
                    if (index < block.receiptsSize())
                    {
                        block.setReceipt(index, receipt);
                    }
                    else
                    {
                        block.appendReceipt(receipt);
                    }
                }
                newBlockHeader.setGasUsed(totalGas);
            },
            [&]() {
                newBlockHeader.setStateRoot(
                    task::syncWait(m_schedulerImpl.finish(blockHeader, m_hashImpl)));
            },
            [&]() {
                auto hasher = m_hashImpl.hasher();
                bcos::crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(
                    std::move(hasher));
                auto hashesRange = receipts | RANGES::views::transform([](const auto& receipt) {
                    return receipt->hash();
                });

                std::vector<bcos::h256> merkleTrie;
                merkle.generateMerkle(hashesRange, merkleTrie);
                newBlockHeader.setReceiptsRoot(*RANGES::rbegin(merkleTrie));
            });
    }

    auto current() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

public:
    BaselineScheduler(SchedulerImpl& schedulerImpl, protocol::BlockHeaderFactory& blockFactory,
        Ledger& ledger, txpool::TxPoolInterface& txPool,
        protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory,
        crypto::Hash const& hashImpl)
      : m_schedulerImpl(schedulerImpl),
        m_blockHeaderFactory(blockFactory),
        m_ledger(ledger),
        m_txpool(txPool),
        m_transactionSubmitResultFactory(transactionSubmitResultFactory),
        m_hashImpl(hashImpl)
    {}
    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) noexcept = default;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) noexcept = default;
    ~BaselineScheduler() noexcept override = default;

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool sysBlock)>
            callback) override
    {
        task::wait([](decltype(this) self, bcos::protocol::Block::Ptr block, bool verify,
                       decltype(callback) callback) -> task::Task<void> {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().EXECUTE_BLOCK);
            try
            {
                auto blockHeader = block->blockHeaderConst();
                std::unique_lock executeLock(self->m_executeMutex, std::try_to_lock);
                if (!executeLock.owns_lock())
                {
                    auto message = fmt::format(
                        "Another block:{} is executing!", self->m_lastExecutedBlockNumber);
                    report.release();
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                        nullptr, false);

                    co_return;
                }

                if (self->m_lastExecutedBlockNumber != -1 &&
                    blockHeader->number() - self->m_lastExecutedBlockNumber != 1)
                {
                    auto message =
                        fmt::format("Discontinuous execute block number! expect: {} input: {}",
                            self->m_lastExecutedBlockNumber + 1, blockHeader->number());
                    executeLock.unlock();
                    BASELINE_SCHEDULER_LOG(INFO) << message;
                    report.release();
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 scheduler::SchedulerError::InvalidBlockNumber, message),
                        nullptr, false);
                    co_return;
                }

                auto now = self->current();
                BASELINE_SCHEDULER_LOG(INFO)
                    << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                    << block->transactionsMetaDataSize() << " | " << block->transactionsSize();

                // start calucate transaction root
                std::promise<bcos::h256> transactionRootPromise;
                self->m_asyncGroup.run([&]() {
                    auto hasher = self->m_hashImpl.hasher();

                    try
                    {
                        bcos::crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>>
                            merkle(hasher.clone());
                        std::vector<bcos::h256> merkleTrie;
                        if (block->transactionsSize() > 0)
                        {
                            auto hashes =
                                RANGES::iota_view<size_t, size_t>(0LU, block->transactionsSize()) |
                                RANGES::views::transform([&block](uint64_t index) {
                                    return block->transaction(index)->hash();
                                });
                            merkle.generateMerkle(hashes, merkleTrie);
                        }
                        else
                        {
                            auto hashes = RANGES::iota_view<size_t, size_t>(
                                              0LU, block->transactionsMetaDataSize()) |
                                          RANGES::views::transform([&block](uint64_t index) {
                                              return block->transactionHash(index);
                                          });
                            merkle.generateMerkle(hashes, merkleTrie);
                        }
                        // TODO: write merkle into storage
                        transactionRootPromise.set_value(*RANGES::rbegin(merkleTrie));
                    }
                    catch (...)
                    {
                        transactionRootPromise.set_exception(std::current_exception());
                    }
                });

                self->m_schedulerImpl.start();
                auto transactions = co_await self->getTransactions(*block);
                auto receipts = co_await self->m_schedulerImpl.execute(*blockHeader,
                    transactions |
                        RANGES::views::transform(
                            [](protocol::Transaction::ConstPtr const& transactionPtr)
                                -> protocol::Transaction const& { return *transactionPtr; }));

                auto newBlockHeader = self->m_blockHeaderFactory.populateBlockHeader(blockHeader);
                {
                    self->finishExecute(receipts, *blockHeader, *newBlockHeader, *block);
                    auto transactionRootFuture = transactionRootPromise.get_future();
                    newBlockHeader->setTxsRoot(transactionRootFuture.get());
                    newBlockHeader->calculateHash(self->m_hashImpl);
                    newBlockHeader->calculateHash(self->m_hashImpl);
                }

                if (verify && newBlockHeader->hash() != blockHeader->hash())
                {
                    auto message = fmt::format("Unmatch block hash! Expect: {} got: {}",
                        blockHeader->hash().hex(), newBlockHeader->hash().hex());
                    BASELINE_SCHEDULER_LOG(ERROR) << message;

                    executeLock.unlock();
                    report.release();
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                        nullptr, false);
                    co_return;
                }

                self->m_lastExecutedBlockNumber = blockHeader->number();

                std::unique_lock resultsLock(self->m_resultsMutex);
                self->m_results.push_front(
                    {.m_transactions = std::move(transactions), .m_block = std::move(block)});
                resultsLock.unlock();
                executeLock.unlock();

                BASELINE_SCHEDULER_LOG(INFO)
                    << "Execute block finished: " << newBlockHeader->number() << " | "
                    << newBlockHeader->hash() << " | " << newBlockHeader->stateRoot() << " | "
                    << newBlockHeader->txsRoot() << " | " << newBlockHeader->receiptsRoot() << " | "
                    << newBlockHeader->gasUsed() << " | " << (self->current() - now) << "ms";

                report.release();
                callback(nullptr, std::move(newBlockHeader), false);
                co_return;
            }
            catch (std::exception& e)
            {
                auto message =
                    fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
                BASELINE_SCHEDULER_LOG(ERROR) << message;

                report.release();
                callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr, false);
            }
        }(this, std::move(block), verify, std::move(callback)));
    }

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::BlockHeader::Ptr blockHeader,
                       decltype(callback) callback) -> task::Task<void> {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().COMMIT_BLOCK);
            try
            {
                BASELINE_SCHEDULER_LOG(INFO) << "Commit block: " << blockHeader->number();

                std::unique_lock commitLock(self->m_commitMutex, std::try_to_lock);
                if (!commitLock.owns_lock())
                {
                    auto message = fmt::format(
                        "Another block:{} is committing!", self->m_lastcommittedBlockNumber);
                    BASELINE_SCHEDULER_LOG(INFO) << message;

                    report.release();
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

                    report.release();
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 scheduler::SchedulerError::InvalidBlockNumber, message),
                        nullptr);
                    co_return;
                }

                std::unique_lock resultsLock(self->m_resultsMutex);
                if (self->m_results.empty())
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error("Unexpected empty results!"));
                }

                auto result = std::move(self->m_results.back());
                self->m_results.pop_back();
                resultsLock.unlock();

                if (blockHeader->number() != 0)
                {
                    result.m_block->setBlockHeader(blockHeader);
                    // Write block and receipt
                    co_await self->m_ledger.template setBlock<concepts::ledger::HEADER,
                        concepts::ledger::TRANSACTIONS_METADATA, concepts::ledger::RECEIPTS,
                        concepts::ledger::NONCES>(*(result.m_block));
                }
                else
                {
                    BASELINE_SCHEDULER_LOG(INFO) << "Ignore commit block header: 0";
                }

                // Write states
                co_await self->m_schedulerImpl.commit();
                auto ledgerConfig =
                    std::make_shared<ledger::LedgerConfig>(co_await self->m_ledger.getConfig());
                ledgerConfig->setHash(blockHeader->hash());
                BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << blockHeader->number();
                commitLock.unlock();

                self->m_asyncGroup.run([self = self, result = std::move(result)]() {
                    ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                        ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

                    auto blockHeader = result.m_block->blockHeaderConst();
                    auto submitResults =
                        RANGES::views::iota(0LU, result.m_block->receiptsSize()) |
                        RANGES::views::transform(
                            [&](uint64_t index) -> protocol::TransactionSubmitResult::Ptr {
                                auto& transaction = result.m_transactions[index];
                                auto receipt = result.m_block->receipt(index);

                                auto submitResult =
                                    self->m_transactionSubmitResultFactory.createTxSubmitResult();
                                submitResult->setStatus(receipt->status());
                                submitResult->setTxHash(result.m_block->transactionHash(index));
                                submitResult->setBlockHash(blockHeader->hash());
                                submitResult->setTransactionIndex(index);
                                submitResult->setNonce(transaction->nonce());
                                submitResult->setTransactionReceipt(std::move(receipt));
                                submitResult->setSender(std::string(transaction->sender()));
                                submitResult->setTo(std::string(transaction->to()));

                                return submitResult;
                            }) |
                        RANGES::to<std::vector<protocol::TransactionSubmitResult::Ptr>>();

                    auto submitResultsPtr =
                        std::make_shared<bcos::protocol::TransactionSubmitResults>(
                            std::move(submitResults));
                    self->m_blockNumberNotifier(blockHeader->number());
                    self->m_transactionNotifier(blockHeader->number(), std::move(submitResultsPtr),
                        [](const Error::Ptr& error) {
                            if (error)
                            {
                                BASELINE_SCHEDULER_LOG(WARNING)
                                    << "Push block notify error!"
                                    << boost::diagnostic_information(*error);
                            }
                        });
                });

                report.release();
                callback(nullptr, std::move(ledgerConfig));
                co_return;
            }
            catch (std::exception& e)
            {
                auto message =
                    fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
                BASELINE_SCHEDULER_LOG(ERROR) << message;

                report.release();
                callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr);
            }
        }(this, std::move(header), std::move(callback)));
    }

    void status(
        [[maybe_unused]] std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)>
            callback) override
    {
        callback({}, {});
    }

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                       decltype(callback) callback) -> task::Task<void> {
            // TODO: Use real block number and storage block header version
            auto blockHeader = self->m_blockHeaderFactory.createBlockHeader();
            blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::V3_3_VERSION);
            auto receipt = co_await self->m_schedulerImpl.call(*blockHeader, *transaction);

            callback(nullptr, std::move(receipt));
        }(this, std::move(transaction), std::move(callback)));
    }

    void reset([[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {
        callback(nullptr);
    }

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }

    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {
        callback(nullptr);
    }

    void stop() override{};

    void registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier)
    {
        m_transactionNotifier = std::move(txNotifier);
    }

    void registerBlockNumberNotifier(
        std::function<void(bcos::protocol::BlockNumber)> blockNumberNotifier)
    {
        m_blockNumberNotifier = std::move(blockNumberNotifier);
    }
};

}  // namespace bcos::transaction_scheduler