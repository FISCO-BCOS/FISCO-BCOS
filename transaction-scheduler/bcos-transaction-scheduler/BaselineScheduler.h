#pragma once

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-utilities/Common.h"
#include <bcos-concepts/ledger/Ledger.h>
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

template <class MultiLayerStorage, class Executor, class SchedulerImpl,
    concepts::ledger::IsLedger Ledger>
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
    int64_t m_lastcommittedBlockNumber = -1;
    std::mutex m_commitMutex;

    tbb::task_group m_notifyGroup;
    tbb::task_group m_transactionRootGroup;

    struct ExecuteResult
    {
        std::vector<protocol::Transaction::ConstPtr> m_transactions;
        protocol::Block::Ptr m_block;
    };
    std::list<ExecuteResult> m_results;
    std::mutex m_resultsMutex;

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block) const
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

    task::Task<void> writeBlockAndTransactions(auto& storage,
        concepts::ledger::IsLedger auto& ledger, protocol::Block& block,
        std::vector<protocol::Transaction::ConstPtr> const& transactions)
    {
        if (block.blockHeaderConst()->number() != 0)
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SET_BLOCK);

            co_await ledger.template setBlock<concepts::ledger::HEADER,
                concepts::ledger::TRANSACTIONS_METADATA, concepts::ledger::RECEIPTS,
                concepts::ledger::NONCES>(storage, block);

            std::vector<bcos::h256> hashes(RANGES::size(transactions));
            std::vector<std::vector<bcos::byte>> buffers(RANGES::size(transactions));
            tbb::parallel_for(
                tbb::blocked_range(0LU, RANGES::size(transactions)), [&](auto const& range) {
                    for (auto i = range.begin(); i != range.end(); ++i)
                    {
                        auto& transaction = transactions[i];
                        hashes[i] = transaction->hash();
                        bcos::concepts::serialize::encode(*transaction, buffers[i]);
                    }
                });

            co_await ledger.template setTransactions<true>(
                storage, std::move(hashes), std::move(buffers));
        }
    }

    bcos::h256 calcauteTransactionRoot(protocol::Block const& block)
    {
        auto hasher = m_hashImpl.hasher();
        bcos::crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(
            hasher.clone());
        std::vector<bcos::h256> merkleTrie;
        if (block.transactionsSize() > 0)
        {
            auto hashes = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsSize()) |
                          RANGES::views::transform([&block](uint64_t index) {
                              return block.transaction(index)->hash();
                          });
            merkle.generateMerkle(hashes, merkleTrie);
        }
        else
        {
            auto hashes = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
                          RANGES::views::transform(
                              [&block](uint64_t index) { return block.transactionHash(index); });
            merkle.generateMerkle(hashes, merkleTrie);
        }
        // TODO: write merkle into storage
        return *RANGES::rbegin(merkleTrie);
    }

    task::Task<bcos::h256> calcauteStateRoot(
        auto& storage, protocol::BlockHeader const& blockHeader, crypto::Hash const& hashImpl)
    {
        auto it = co_await storage.seek(storage2::STORAGE_BEGIN);

        static constexpr int HASH_CHUNK_SIZE = 32;
        auto range = it.range() | RANGES::views::chunk(HASH_CHUNK_SIZE);

        tbb::combinable<bcos::h256> combinableHash;
        tbb::task_group taskGroup;
        for (auto&& subrange : range)
        {
            taskGroup.run([subrange = std::forward<decltype(subrange)>(subrange), &combinableHash,
                              &blockHeader, &hashImpl]() {
                auto& entryHash = combinableHash.local();

                for (auto const& keyValue : subrange)
                {
                    auto& [key, entry] = keyValue;
                    auto& [tableName, keyName] = *key;
                    if (entry)
                    {
                        entryHash ^=
                            entry->hash(tableName, keyName, hashImpl, blockHeader.version());
                    }
                    else
                    {
                        storage::Entry deleteEntry;
                        deleteEntry.setStatus(storage::Entry::DELETED);
                        entryHash ^=
                            deleteEntry.hash(tableName, keyName, hashImpl, blockHeader.version());
                    }
                }
            });
        }
        taskGroup.wait();
        m_multiLayerStorage.pushMutableToImmutableFront();

        co_return combinableHash.combine(
            [](const bcos::h256& lhs, const bcos::h256& rhs) -> bcos::h256 { return lhs ^ rhs; });
    }

    void finishExecute(auto& storage, RANGES::range auto const& receipts,
        protocol::BlockHeader const& blockHeader, protocol::BlockHeader& newBlockHeader,
        protocol::Block& newBlock)
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
                newBlockHeader.setStateRoot(
                    task::syncWait(calcauteStateRoot(storage, blockHeader, m_hashImpl)));
            },
            [&]() {
                // Calcaute receipts root
                bcos::crypto::merkle::Merkle merkle(m_hashImpl.hasher());
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

    task::Task<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        bcos::protocol::Block::Ptr block, bool verify)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_BLOCK);
        try
        {
            auto blockHeader = block->blockHeaderConst();
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message =
                    fmt::format("Another block:{} is executing!", m_lastExecutedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false);
            }

            if (m_lastExecutedBlockNumber != -1 &&
                blockHeader->number() - m_lastExecutedBlockNumber != 1)
            {
                auto message =
                    fmt::format("Discontinuous execute block number! expect: {} input: {}",
                        m_lastExecutedBlockNumber + 1, blockHeader->number());
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
            m_transactionRootGroup.run([&]() {
                try
                {
                    transactionRootPromise.set_value(calcauteTransactionRoot(*block));
                }
                catch (...)
                {
                    transactionRootPromise.set_exception(std::current_exception());
                }
            });

            m_multiLayerStorage.newMutable();
            auto view = m_multiLayerStorage.fork(true);
            auto transactions = co_await getTransactions(*block);
            auto receipts = co_await transaction_scheduler::execute(m_schedulerImpl, view,
                m_executor, *blockHeader,
                transactions |
                    RANGES::views::transform(
                        [](protocol::Transaction::ConstPtr const& transactionPtr)
                            -> protocol::Transaction const& { return *transactionPtr; }));

            auto newBlockHeader = m_blockHeaderFactory.populateBlockHeader(blockHeader);
            finishExecute(m_multiLayerStorage.mutableStorage(), receipts, *blockHeader,
                *newBlockHeader, *block);
            newBlockHeader->setTxsRoot(transactionRootFuture.get());
            newBlockHeader->calculateHash(m_hashImpl);

            if (verify && newBlockHeader->hash() != blockHeader->hash())
            {
                auto message = fmt::format("Unmatch block hash! Expect: {} got: {}",
                    blockHeader->hash().hex(), newBlockHeader->hash().hex());
                BASELINE_SCHEDULER_LOG(ERROR) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false);
            }

            m_lastExecutedBlockNumber = blockHeader->number();

            std::unique_lock resultsLock(m_resultsMutex);
            m_results.push_front(
                {.m_transactions = std::move(transactions), .m_block = std::move(block)});

            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block finished: " << newBlockHeader->number() << " | "
                << newBlockHeader->hash() << " | " << newBlockHeader->stateRoot() << " | "
                << newBlockHeader->txsRoot() << " | " << newBlockHeader->receiptsRoot() << " | "
                << newBlockHeader->gasUsed() << " | " << (current() - now) << "ms";

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

    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        protocol::BlockHeader::Ptr header)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().COMMIT_BLOCK);
        try
        {
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block: " << header->number();

            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message =
                    fmt::format("Another block:{} is committing!", m_lastcommittedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr);
            }

            if (m_lastcommittedBlockNumber != -1 &&
                header->number() - m_lastcommittedBlockNumber != 1)
            {
                auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                    header->number(), m_lastcommittedBlockNumber + 1);

                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr);
            }

            std::unique_lock resultsLock(m_resultsMutex);
            if (m_results.empty())
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("Unexpected empty results!"));
            }

            auto result = std::move(m_results.back());
            m_results.pop_back();
            resultsLock.unlock();

            result.m_block->setBlockHeader(header);
            auto lastStorage = m_multiLayerStorage.lastImmutableStorage();
            co_await writeBlockAndTransactions(
                *lastStorage, m_ledger, *(result.m_block), result.m_transactions);
            co_await m_multiLayerStorage.mergeAndPopImmutableBack();

            // Write states
            auto ledgerConfig =
                std::make_shared<ledger::LedgerConfig>(co_await m_ledger.getConfig());
            ledgerConfig->setHash(header->hash());
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << header->number();
            commitLock.unlock();

            m_notifyGroup.run([this, result = std::move(result)]() {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

                auto blockHeader = result.m_block->blockHeaderConst();
                auto submitResults =
                    RANGES::views::iota(0LU, result.m_block->receiptsSize()) |
                    RANGES::views::transform(
                        [&, this](uint64_t index) -> protocol::TransactionSubmitResult::Ptr {
                            auto& transaction = result.m_transactions[index];
                            auto receipt = result.m_block->receipt(index);

                            auto submitResult =
                                m_transactionSubmitResultFactory.createTxSubmitResult();
                            submitResult->setStatus(receipt->status());
                            submitResult->setTxHash(result.m_block->transactionHash(index));
                            submitResult->setBlockHash(blockHeader->hash());
                            submitResult->setTransactionIndex(static_cast<int64_t>(index));
                            submitResult->setNonce(transaction->nonce());
                            submitResult->setTransactionReceipt(std::move(receipt));
                            submitResult->setSender(std::string(transaction->sender()));
                            submitResult->setTo(std::string(transaction->to()));

                            return submitResult;
                        }) |
                    RANGES::to<std::vector<protocol::TransactionSubmitResult::Ptr>>();

                auto submitResultsPtr = std::make_shared<bcos::protocol::TransactionSubmitResults>(
                    std::move(submitResults));
                m_blockNumberNotifier(blockHeader->number());
                m_transactionNotifier(blockHeader->number(), std::move(submitResultsPtr),
                    [](const Error::Ptr& error) {
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
            std::apply(callback, co_await self->coExecuteBlock(std::move(block), verify));
        }(this, std::move(block), verify, std::move(callback)));
    }

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback) override
    {
        task::wait([](decltype(this) self, protocol::BlockHeader::Ptr blockHeader,
                       decltype(callback) callback) -> task::Task<void> {
            std::apply(callback, co_await self->coCommitBlock(std::move(blockHeader)));
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