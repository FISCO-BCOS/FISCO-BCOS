#pragma once

#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-ledger/src/libledger/LedgerMethods.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ITTAPI.h>
#include <fmt/format.h>
#include <ittnotify.h>
#include <oneapi/tbb/combinable.h>
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

/**
 * Retrieves a vector of transactions from the provided transaction pool and block.
 *
 * @param txpool The transaction pool to retrieve transactions from.
 * @param block The block to retrieve transactions for.
 * @return A task that resolves to a vector of transactions.
 */
task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
    txpool::TxPoolInterface& txpool, protocol::Block& block);

/**
 * Calculates the transaction root hash for a given block using the specified hash implementation.
 *
 * @param block The block for which to calculate the transaction root hash.
 * @param hashImpl The hash implementation to use for the calculation.
 * @return The calculated transaction root hash.
 */
bcos::h256 calcauteTransactionRoot(protocol::Block const& block, crypto::Hash const& hashImpl);

/**
 * Returns the current time in milliseconds since the epoch.
 *
 * @return the current time in milliseconds
 */
std::chrono::milliseconds::rep current();

template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
class BaselineScheduler : public scheduler::SchedulerInterface
{
private:
    MultiLayerStorage& m_multiLayerStorage;
    SchedulerImpl& m_schedulerImpl;
    Executor& m_executor;
    protocol::BlockHeaderFactory& m_blockHeaderFactory;
    Ledger& m_ledger;
    txpool::TxPoolInterface& m_txpool;
    protocol::TransactionSubmitResultFactory& m_transactionSubmitResultFactory;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_transactionNotifier;
    crypto::Hash const& m_hashImpl;
    int64_t m_lastExecutedBlockNumber = -1;
    std::mutex m_executeMutex;
    int64_t m_lastCommittedBlockNumber = -1;
    std::mutex m_commitMutex;
    tbb::task_group m_notifyGroup;

    struct ExecuteResult
    {
        protocol::TransactionsPtr m_transactions;
        std::vector<protocol::TransactionReceipt::Ptr> m_receipts;
        protocol::Block::Ptr m_block;
    };
    std::list<ExecuteResult> m_results;
    std::mutex m_resultsMutex;

    /**
     * Calculates the state root hash for a given block header using the BaselineScheduler and
     * storage.
     *
     * @param scheduler The BaselineScheduler instance to use for calculating the state root.
     * @param storage The storage instance to use for retrieving the necessary data.
     * @param blockHeader The block header for which to calculate the state root.
     * @param hashImpl The hash implementation to use for hashing the state trie nodes.
     * @return A task that resolves to the calculated state root hash.
     */
    friend task::Task<bcos::h256> calculateStateRoot(BaselineScheduler& scheduler, auto& storage,
        protocol::BlockHeader const& blockHeader, crypto::Hash const& hashImpl)
    {
        auto blockVersion = true ? (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION :
                                   (uint32_t)bcos::protocol::BlockVersion::V3_0_VERSION;

        auto it = co_await storage.seek(storage2::STORAGE_BEGIN);

        static constexpr int HASH_CHUNK_SIZE = 32;
        auto range = it.range() | RANGES::views::chunk(HASH_CHUNK_SIZE);

        tbb::combinable<bcos::h256> combinableHash;
        tbb::task_group taskGroup;
        for (auto&& subrange : range)
        {
            taskGroup.run([&]() {
                auto& entryHash = combinableHash.local();

                for (auto const& keyValue : subrange)
                {
                    auto [key, entry] = keyValue;
                    auto& [tableName, keyName] = *key;

                    std::optional<storage::Entry> deletedEntry;
                    if (!entry)
                    {
                        deletedEntry.emplace();
                        deletedEntry->setStatus(storage::Entry::DELETED);
                        entry = std::addressof(*deletedEntry);
                    }

                    if (blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
                    {
                        entryHash ^= entry->hash(tableName, keyName, hashImpl, blockVersion);
                    }
                    else
                    {
                        entryHash ^= hashImpl.hash(tableName) ^ hashImpl.hash(keyName) ^
                                     entry->hash(tableName, keyName, hashImpl, blockVersion);
                    }
                }
            });
        }
        taskGroup.wait();
        scheduler.m_multiLayerStorage.pushMutableToImmutableFront();

        co_return combinableHash.combine(
            [](const bcos::h256& lhs, const bcos::h256& rhs) -> bcos::h256 { return lhs ^ rhs; });
    }

    /**
     * @brief Finishes executing transactions and updates the block header and block with the
     * receipts.
     *
     * @tparam StorageType The type of the storage object.
     * @tparam ReceiptsRange The type of the range of receipts.
     * @param storage The storage object.
     * @param receipts The range of receipts.
     * @param blockHeader The original block header.
     * @param newBlockHeader The updated block header.
     * @param newBlock The updated block.
     */
    friend void finishExecute(BaselineScheduler& scheduler, auto& storage,
        RANGES::range auto const& receipts, protocol::BlockHeader const& blockHeader,
        protocol::BlockHeader& newBlockHeader, protocol::Block& newBlock)
    {
        ittapi::Report finishReport(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().FINISH_EXECUTE);

        tbb::parallel_invoke(
            [&]() {
                // Append receipts
                bcos::u256 totalGas = 0;
                for (auto&& [receipt, index] :
                    RANGES::views::zip(receipts, RANGES::views::iota(0UL)))
                {
                    totalGas += receipt->gasUsed();
                    if (index < newBlock.receiptsSize())
                    {
                        newBlock.setReceipt(index, receipt);
                    }
                    else
                    {
                        newBlock.appendReceipt(receipt);
                    }
                }
                newBlockHeader.setGasUsed(totalGas);
            },
            [&]() {
                // Calcaute state root
                newBlockHeader.setStateRoot(task::syncWait(
                    calculateStateRoot(scheduler, storage, blockHeader, scheduler.m_hashImpl)));
            },
            [&]() {
                // Calcaute receipts root
                bcos::crypto::merkle::Merkle merkle(scheduler.m_hashImpl.hasher());
                auto hashesRange = receipts | RANGES::views::transform([](const auto& receipt) {
                    return receipt->hash();
                });

                std::vector<bcos::h256> merkleTrie;
                merkle.generateMerkle(hashesRange, merkleTrie);
                newBlockHeader.setReceiptsRoot(*RANGES::rbegin(merkleTrie));
            });
    }

    /**
     * Executes a block and returns a tuple containing an error (if any), the block header, and a
     * boolean indicating success.
     *
     * @param block The block to execute.
     * @param verify Whether to verify the block before executing it.
     * @return A tuple containing an error (if any), the block header, and a boolean indicating
     * success.
     */
    friend task::Task<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool>>
    coExecuteBlock(BaselineScheduler& scheduler, bcos::protocol::Block::Ptr block, bool verify)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_BLOCK);
        try
        {
            auto blockHeader = block->blockHeaderConst();
            std::unique_lock executeLock(scheduler.m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = fmt::format(
                    "Another block:{} is executing!", scheduler.m_lastExecutedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false);
            }

            if (scheduler.m_lastExecutedBlockNumber != -1 &&
                blockHeader->number() - scheduler.m_lastExecutedBlockNumber != 1)
            {
                auto message =
                    fmt::format("Discontinuous execute block number! expect: {} input: {}",
                        scheduler.m_lastExecutedBlockNumber + 1, blockHeader->number());
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false);
            }

            auto now = current();
            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                << block->transactionsMetaDataSize() << " | " << block->transactionsSize();

            // start calucate transaction root
            std::promise<bcos::h256> transactionRootPromise;
            auto transactionRootFuture = transactionRootPromise.get_future();
            tbb::task_group transactionRootGroup;
            transactionRootGroup.run([&]() {
                try
                {
                    transactionRootPromise.set_value(
                        calcauteTransactionRoot(*block, scheduler.m_hashImpl));
                }
                catch (...)
                {
                    transactionRootPromise.set_exception(std::current_exception());
                }
            });

            scheduler.m_multiLayerStorage.newMutable();
            auto view = scheduler.m_multiLayerStorage.fork(true);
            auto constTransactions = co_await getTransactions(scheduler.m_txpool, *block);
            auto transactions =
                RANGES::views::transform(constTransactions,
                    [](protocol::Transaction::ConstPtr const& transaction) {
                        return std::const_pointer_cast<protocol::Transaction>(transaction);
                    }) |
                RANGES::to<std::vector<protocol::Transaction::Ptr>>();
            auto receipts = co_await transaction_scheduler::execute(scheduler.m_schedulerImpl, view,
                scheduler.m_executor, *blockHeader,
                transactions |
                    RANGES::views::transform(
                        [](protocol::Transaction::Ptr const& transactionPtr)
                            -> protocol::Transaction const& { return *transactionPtr; }));

            auto newBlockHeader = scheduler.m_blockHeaderFactory.populateBlockHeader(blockHeader);
            finishExecute(scheduler, scheduler.m_multiLayerStorage.mutableStorage(), receipts,
                *blockHeader, *newBlockHeader, *block);
            newBlockHeader->setTxsRoot(transactionRootFuture.get());
            newBlockHeader->calculateHash(scheduler.m_hashImpl);

            if (verify && newBlockHeader->hash() != blockHeader->hash())
            {
                auto message = fmt::format("Unmatch block hash! Expect: {} got: {}",
                    blockHeader->hash().hex(), newBlockHeader->hash().hex());
                BASELINE_SCHEDULER_LOG(ERROR) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false);
            }

            scheduler.m_lastExecutedBlockNumber = blockHeader->number();

            std::unique_lock resultsLock(scheduler.m_resultsMutex);
            scheduler.m_results.push_front(
                {.m_transactions =
                        std::make_shared<protocol::Transactions>(std::move(transactions)),
                    .m_receipts = std::move(receipts),
                    .m_block = std::move(block)});

            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block finished: " << newBlockHeader->version()
                << newBlockHeader->number() << " | " << newBlockHeader->hash() << " | "
                << newBlockHeader->stateRoot() << " | " << newBlockHeader->txsRoot() << " | "
                << newBlockHeader->receiptsRoot() << " | " << newBlockHeader->gasUsed() << " | "
                << (current() - now) << "ms";

            co_return std::make_tuple(nullptr, std::move(newBlockHeader), false);
        }
        catch (std::exception& e)
        {
            auto message =
                fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
            BASELINE_SCHEDULER_LOG(ERROR) << message;

            co_return std::make_tuple(
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message), nullptr,
                false);
        }
    }

    /**
     * Commits a block to the ledger and returns an error object and a ledger configuration object.
     *
     * @param header A shared pointer to the block header to be committed.
     * @return A task that returns a tuple containing an error object and a ledger configuration
     * object.
     */
    friend task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        BaselineScheduler& scheduler, protocol::BlockHeader::Ptr header)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().COMMIT_BLOCK);
        try
        {
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block: " << header->number();

            std::unique_lock commitLock(scheduler.m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = fmt::format(
                    "Another block:{} is committing!", scheduler.m_lastCommittedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr);
            }

            if (scheduler.m_lastCommittedBlockNumber != -1 &&
                header->number() - scheduler.m_lastCommittedBlockNumber != 1)
            {
                auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                    header->number(), scheduler.m_lastCommittedBlockNumber + 1);

                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr);
            }

            std::unique_lock resultsLock(scheduler.m_resultsMutex);
            if (scheduler.m_results.empty())
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("Unexpected empty results!"));
            }

            auto result = std::move(scheduler.m_results.back());
            scheduler.m_results.pop_back();
            resultsLock.unlock();

            result.m_block->setBlockHeader(header);
            auto lastStorage = scheduler.m_multiLayerStorage.lastImmutableStorage();
            if (result.m_block->blockHeaderConst()->number() != 0)
            {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().SET_BLOCK);

                co_await ledger::prewriteBlock(
                    scheduler.m_ledger, result.m_transactions, result.m_block, true, *lastStorage);
            }
            co_await scheduler.m_multiLayerStorage.mergeAndPopImmutableBack();

            // Write states
            auto ledgerConfig = co_await ledger::getLedgerConfig(scheduler.m_ledger);
            ledgerConfig->setHash(header->hash());
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << header->number();
            commitLock.unlock();

            scheduler.m_notifyGroup.run([&, result = std::move(result),
                                            blockHash = ledgerConfig->hash(),
                                            blockNumber = ledgerConfig->blockNumber()]() {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

                auto submitResults =
                    RANGES::views::zip(
                        RANGES::views::iota(0), *result.m_transactions, result.m_receipts) |
                    RANGES::views::transform(
                        [&](auto input) -> protocol::TransactionSubmitResult::Ptr {
                            auto&& [index, transaction, receipt] = input;

                            auto submitResult =
                                scheduler.m_transactionSubmitResultFactory.createTxSubmitResult();
                            submitResult->setStatus(receipt->status());
                            submitResult->setTxHash(transaction->hash());
                            submitResult->setBlockHash(blockHash);
                            submitResult->setTransactionIndex(static_cast<int64_t>(index));
                            submitResult->setNonce(transaction->nonce());
                            submitResult->setTransactionReceipt(receipt);
                            submitResult->setSender(std::string(transaction->sender()));
                            submitResult->setTo(std::string(transaction->to()));

                            return submitResult;
                        }) |
                    RANGES::to<std::vector<protocol::TransactionSubmitResult::Ptr>>();

                auto submitResultsPtr = std::make_shared<bcos::protocol::TransactionSubmitResults>(
                    std::move(submitResults));
                scheduler.m_blockNumberNotifier(blockNumber);
                scheduler.m_transactionNotifier(
                    blockNumber, std::move(submitResultsPtr), [](const Error::Ptr& error) {
                        if (error)
                        {
                            BASELINE_SCHEDULER_LOG(WARNING)
                                << "Push block notify error!"
                                << boost::diagnostic_information(*error);
                        }
                    });
            });

            co_return std::make_tuple(Error::Ptr{}, std::move(ledgerConfig));
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
            BASELINE_SCHEDULER_LOG(ERROR) << message;

            co_return std::make_tuple(
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message), nullptr);
        }
    }

public:
    BaselineScheduler(MultiLayerStorage& multiLayerStorage, SchedulerImpl& schedulerImpl,
        Executor& executor, protocol::BlockHeaderFactory& blockFactory, Ledger& ledger,
        txpool::TxPoolInterface& txPool,
        protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory,
        crypto::Hash const& hashImpl)
      : m_multiLayerStorage(multiLayerStorage),
        m_schedulerImpl(schedulerImpl),
        m_executor(executor),
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
            std::apply(callback, co_await coExecuteBlock(*self, std::move(block), verify));
        }(this, std::move(block), verify, std::move(callback)));
    }

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::BlockHeader::Ptr blockHeader,
                       decltype(callback) callback) -> task::Task<void> {
            std::apply(callback, co_await coCommitBlock(*self, std::move(blockHeader)));
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
            auto view = self->m_multiLayerStorage.fork(false);
            view.newTemporaryMutable();
            auto receipt = co_await transaction_executor::execute(
                self->m_executor, view, *blockHeader, *transaction, 0);

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