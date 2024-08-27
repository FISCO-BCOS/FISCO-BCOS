#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/dispatcher/SchedulerTypeDef.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/ITTAPI.h"
#include <fmt/format.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/parallel_reduce.h>
#include <oneapi/tbb/task_group.h>
#include <boost/atomic.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <type_traits>

namespace bcos::transaction_scheduler
{
#define BASELINE_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

struct NotFoundTransactionError : public bcos::Error
{
};

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
 * Calculates the transaction root hash for a given block using the specified hash
 * implementation.
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

/**
 * Calculates the state root of the given storage using the specified hash implementation.
 *
 * @param storage The storage to calculate the state root for.
 * @param hashImpl The hash implementation to use for the calculation.
 * @return A task that will eventually resolve to the calculated state root.
 */
task::Task<h256> calculateStateRoot(
    auto& storage, uint32_t blockVersion, crypto::Hash const& hashImpl)
{
    constexpr static auto STATE_ROOT_CHUNK_SIZE = 64;
    auto range = co_await storage2::range(storage);
    storage::Entry deletedEntry;
    deletedEntry.setStatus(storage::Entry::DELETED);

    tbb::concurrent_vector<h256> hashes;
    tbb::task_group hashGroup;
    while (auto keyValue = co_await range.next())
    {
        hashGroup.run([keyValue = std::move(keyValue), &hashes, &deletedEntry, &hashImpl]() {
            auto [key, entry] = *keyValue;
            transaction_executor::StateKeyView view(key);
            auto [tableName, keyName] = view.getTableAndKey();
            if (!entry)
            {
                entry = std::addressof(deletedEntry);
            }

            hashes.emplace_back(entry->hash(tableName, keyName, hashImpl,
                static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION)));
        });
    }
    hashGroup.wait();

    struct XORHash
    {
        h256 m_hash;
        std::reference_wrapper<decltype(hashes) const> m_hashes;

        XORHash(decltype(hashes) const& hashes) : m_hashes(hashes) {};
        XORHash(XORHash& source, tbb::split /*unused*/) : m_hashes(source.m_hashes) {};
        void operator()(const tbb::blocked_range<size_t>& range)
        {
            for (size_t i = range.begin(); i != range.end(); ++i)
            {
                m_hash ^= m_hashes.get()[i];
            }
        }
        void join(XORHash const& rhs) { m_hash ^= rhs.m_hash; }
    } xorHash(hashes);
    tbb::parallel_reduce(tbb::blocked_range<size_t>(0, hashes.size()), xorHash);
    co_return xorHash.m_hash;
}

task::Task<std::tuple<u256, h256>> calculateReceiptRoot(
    RANGES::range auto const& receipts, protocol::Block& block, crypto::Hash const& hashImpl)
{
    u256 gasUsed;
    h256 receiptRoot;

    tbb::parallel_invoke(
        [&]() {
            for (auto&& [receipt, index] : RANGES::views::zip(receipts, RANGES::views::iota(0UL)))
            {
                gasUsed += receipt->gasUsed();
                if (index < block.receiptsSize())
                {
                    block.setReceipt(index, receipt);
                }
                else
                {
                    block.appendReceipt(receipt);
                }
            }
        },
        [&]() {
            bcos::crypto::merkle::Merkle merkle(hashImpl.hasher());
            auto hashesRange = receipts | RANGES::views::transform(
                                              [](const auto& receipt) { return receipt->hash(); });

            std::vector<bcos::h256> merkleTrie;
            merkle.generateMerkle(hashesRange, merkleTrie);

            receiptRoot = *RANGES::rbegin(merkleTrie);
        });

    co_return std::make_tuple(gasUsed, receiptRoot);
}

/**
 * @brief Finishes the execution of a transaction and updates the block header and block.
 *
 * @param storage The storage object used to store the transaction receipts.
 * @param receipts The range of transaction receipts to be stored.
 * @param blockHeader The original block header.
 * @param newBlockHeader The updated block header.
 * @param newBlock The updated block.
 * @param hashImpl The hash implementation used to calculate the block hash.
 */
void finishExecute(auto& storage, RANGES::range auto const& receipts,
    protocol::BlockHeader& newBlockHeader, protocol::Block& block,
    RANGES::input_range auto const& transactions, bool& sysBlock, crypto::Hash const& hashImpl)
{
    ittapi::Report finishReport(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().FINISH_EXECUTE);
    u256 gasUsed;
    h256 transactionRoot;
    h256 stateRoot;
    h256 receiptRoot;

    tbb::parallel_invoke([&]() { transactionRoot = calcauteTransactionRoot(block, hashImpl); },
        [&]() {
            stateRoot = task::tbb::syncWait(
                calculateStateRoot(storage, block.blockHeaderConst()->version(), hashImpl));
        },
        [&]() {
            std::tie(gasUsed, receiptRoot) =
                task::tbb::syncWait(calculateReceiptRoot(receipts, block, hashImpl));
        },
        [&]() {
            sysBlock = RANGES::any_of(transactions, [](auto const& transaction) {
                return bcos::precompiled::c_systemTxsAddress.contains(transaction->to());
            });
        });
    newBlockHeader.setGasUsed(gasUsed);
    newBlockHeader.setTxsRoot(transactionRoot);
    newBlockHeader.setStateRoot(stateRoot);
    newBlockHeader.setReceiptsRoot(receiptRoot);
    newBlockHeader.calculateHash(hashImpl);
}

template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
class BaselineScheduler : public scheduler::SchedulerInterface
{
private:
    std::reference_wrapper<MultiLayerStorage> m_multiLayerStorage;
    std::reference_wrapper<std::remove_reference_t<SchedulerImpl>> m_schedulerImpl;
    std::reference_wrapper<Executor> m_executor;
    std::reference_wrapper<protocol::BlockHeaderFactory> m_blockHeaderFactory;
    std::reference_wrapper<Ledger> m_ledger;
    std::reference_wrapper<txpool::TxPoolInterface> m_txpool;
    std::reference_wrapper<protocol::TransactionSubmitResultFactory>
        m_transactionSubmitResultFactory;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_transactionNotifier;
    std::reference_wrapper<crypto::Hash const> m_hashImpl;
    std::shared_ptr<ledger::LedgerConfig> m_ledgerConfig;
    std::mutex m_ledgerConfigMutex;

    int64_t m_lastExecutedBlockNumber = -1;
    std::mutex m_executeMutex;
    int64_t m_lastcommittedBlockNumber = -1;
    std::mutex m_commitMutex;
    tbb::task_group m_asyncGroup;

    struct ExecuteResult
    {
        protocol::ConstTransactionsPtr m_transactions;
        std::vector<protocol::TransactionReceipt::Ptr> m_receipts;
        protocol::BlockHeader::Ptr m_executedBlockHeader;
        protocol::Block::Ptr m_block;
        bool m_sysBlock{};
    };
    std::deque<ExecuteResult> m_results;
    std::mutex m_resultsMutex;

    /**
     * Executes a block and returns a tuple containing an error (if any), the block header, and
     * a boolean indicating success.
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
            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                << block->transactionsMetaDataSize() << " | " << block->transactionsSize();

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
                // 如果区块已经执行过，则直接返回结果，不报错，用于共识和同步同时执行一个区块的场景
                // If the block has been executed, the result will be returned directly without
                // error, which is used for the scenario of consensus and synchronous execution of a
                // block at the same time
                {
                    std::unique_lock resultsLock(scheduler.m_resultsMutex);
                    if (!scheduler.m_results.empty())
                    {
                        auto& front = scheduler.m_results.front();
                        auto& back = scheduler.m_results.back();
                        auto number = blockHeader->number();
                        if (number >= front.m_executedBlockHeader->number() &&
                            number <= back.m_executedBlockHeader->number())
                        {
                            BASELINE_SCHEDULER_LOG(INFO)
                                << "Block has been executed, return result directly";
                            auto& result = scheduler.m_results.at(
                                number - front.m_executedBlockHeader->number());
                            co_return std::make_tuple(
                                nullptr, result.m_executedBlockHeader, result.m_sysBlock);
                        }
                    }
                }

                auto message =
                    fmt::format("Discontinuous execute block number! expect: {} input: {}",
                        scheduler.m_lastExecutedBlockNumber + 1, blockHeader->number());
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false);
            }

            auto now = current();
            auto view = fork(scheduler.m_multiLayerStorage.get());
            newMutable(view);
            auto transactions = co_await getTransactions(scheduler.m_txpool.get(), *block);

            ledger::LedgerConfig::Ptr ledgerConfig;
            {
                std::unique_lock ledgerConfigLock(scheduler.m_ledgerConfigMutex);
                ledgerConfig = scheduler.m_ledgerConfig;
            }
            auto receipts = co_await transaction_scheduler::executeBlock(
                scheduler.m_schedulerImpl.get(), view, scheduler.m_executor.get(), *blockHeader,
                transactions | RANGES::views::transform(
                                   [](protocol::Transaction::ConstPtr const& transactionPtr)
                                       -> protocol::Transaction const& { return *transactionPtr; }),
                *ledgerConfig);

            auto executedBlockHeader =
                scheduler.m_blockHeaderFactory.get().populateBlockHeader(blockHeader);
            bool sysBlock = false;
            finishExecute(mutableStorage(view), receipts, *executedBlockHeader, *block,
                transactions, sysBlock, scheduler.m_hashImpl.get());

            if (verify && (executedBlockHeader->hash() != blockHeader->hash()))
            {
                auto message = fmt::format("Sync block error, mismatch block hash: {} | {}",
                    executedBlockHeader->hash().hex(), blockHeader->hash().hex());
                if (executedBlockHeader->stateRoot() != blockHeader->stateRoot())
                {
                    message.append(fmt::format(", state root: {} | {}",
                        executedBlockHeader->stateRoot().hex(), blockHeader->stateRoot().hex()));
                }
                if (executedBlockHeader->txsRoot() != blockHeader->txsRoot())
                {
                    message.append(fmt::format(", tx root: {} | {}",
                        executedBlockHeader->txsRoot().hex(), blockHeader->txsRoot().hex()));
                }
                if (executedBlockHeader->receiptsRoot() != blockHeader->receiptsRoot())
                {
                    message.append(fmt::format(", receipt root: {} | {}",
                        executedBlockHeader->receiptsRoot().hex(),
                        blockHeader->receiptsRoot().hex()));
                }
                BASELINE_SCHEDULER_LOG(ERROR) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false);
            }

            pushView(scheduler.m_multiLayerStorage.get(), std::move(view));
            scheduler.m_lastExecutedBlockNumber = blockHeader->number();

            std::unique_lock resultsLock(scheduler.m_resultsMutex);
            scheduler.m_results.push_front(
                {.m_transactions =
                        std::make_shared<protocol::ConstTransactions>(std::move(transactions)),
                    .m_receipts = std::move(receipts),
                    .m_executedBlockHeader = executedBlockHeader,
                    .m_block = std::move(block),
                    .m_sysBlock = sysBlock});

            BASELINE_SCHEDULER_LOG(INFO)
                << "Execute block finished: " << executedBlockHeader->number() << " | "
                << static_cast<protocol::BlockVersion>(executedBlockHeader->version())
                << " | blockHash: " << executedBlockHeader->hash()
                << " | stateRoot: " << executedBlockHeader->stateRoot()
                << " | txRoot: " << executedBlockHeader->txsRoot()
                << " | receiptRoot: " << executedBlockHeader->receiptsRoot()
                << " | gasUsed: " << executedBlockHeader->gasUsed() << " | sysBlock: " << sysBlock
                << " | elapsed: " << (current() - now) << "ms";

            co_return std::make_tuple(nullptr, std::move(executedBlockHeader), sysBlock);
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
     * Commits a block to the ledger and returns an error object and a ledger configuration
     * object.
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
                    "Another block:{} is committing!", scheduler.m_lastcommittedBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;

                co_return std::make_tuple(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr);
            }

            if (scheduler.m_lastcommittedBlockNumber != -1 &&
                header->number() - scheduler.m_lastcommittedBlockNumber != 1)
            {
                auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                    header->number(), scheduler.m_lastcommittedBlockNumber + 1);

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

            auto now = current();
            auto result = std::move(scheduler.m_results.back());
            scheduler.m_results.pop_back();
            resultsLock.unlock();

            result.m_block->setBlockHeader(header);
            auto lastStorage = backStorage(scheduler.m_multiLayerStorage.get());
            if (result.m_block->blockHeaderConst()->number() != 0)
            {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().SET_BLOCK);

                co_await ledger::prewriteBlock(scheduler.m_ledger.get(), result.m_transactions,
                    result.m_block, false, *lastStorage);
            }
            auto mergedStorage = co_await mergeBackStorage(scheduler.m_multiLayerStorage.get());
            co_await ledger::storeTransactionsAndReceipts(
                scheduler.m_ledger.get(), result.m_transactions, result.m_block);

            auto ledgerConfig = co_await ledger::getLedgerConfig(scheduler.m_ledger.get());
            ledgerConfig->setHash(header->hash());
            {
                std::unique_lock ledgerConfigLock(scheduler.m_ledgerConfigMutex);
                scheduler.m_ledgerConfig = ledgerConfig;
            }
            BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << header->number()
                                         << " | elapsed: " << (current() - now) << "ms";
            commitLock.unlock();

            scheduler.m_asyncGroup.run([&, result = std::move(result),
                                           blockHash = ledgerConfig->hash(),
                                           blockNumber = ledgerConfig->blockNumber()]() {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

                auto submitResults =
                    RANGES::views::zip(
                        RANGES::views::iota(0), *result.m_transactions, result.m_receipts) |
                    RANGES::views::transform([&](auto input)
                                                 -> protocol::TransactionSubmitResult::Ptr {
                        auto&& [index, transaction, receipt] = input;

                        auto submitResult =
                            scheduler.m_transactionSubmitResultFactory.get().createTxSubmitResult();
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
                    RANGES::to<std::vector>();

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
        m_hashImpl(hashImpl),
        m_ledgerConfig(task::syncWait(ledger::getLedgerConfig(m_ledger)))
    {}
    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) noexcept = default;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) noexcept = default;
    ~BaselineScheduler() noexcept override { m_asyncGroup.wait(); }

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
            auto view = fork(self->m_multiLayerStorage.get());
            newMutable(view);
            auto blockHeader = self->m_blockHeaderFactory.get().createBlockHeader();
            ledger::LedgerConfig::Ptr ledgerConfig;
            {
                std::unique_lock ledgerConfigLock(self->m_ledgerConfigMutex);
                ledgerConfig = self->m_ledgerConfig;
            }

            protocol::TransactionReceipt::Ptr receipt;
            if (ledgerConfig)
            {
                blockHeader->setVersion(ledgerConfig->compatibilityVersion());
                blockHeader->setNumber(ledgerConfig->blockNumber() + 1);  // Use next block number
                blockHeader->calculateHash(self->m_hashImpl.get());
                receipt = co_await transaction_executor::executeTransaction(self->m_executor.get(),
                    view, *blockHeader, *transaction, 0, *ledgerConfig, task::syncWait);
            }
            else
            {
                ledger::LedgerConfig emptyLedgerConfig;
                blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::V3_2_4_VERSION);
                blockHeader->calculateHash(self->m_hashImpl.get());
                receipt = co_await transaction_executor::executeTransaction(self->m_executor.get(),
                    view, *blockHeader, *transaction, 0, emptyLedgerConfig, task::syncWait);
            }

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
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            auto view = fork(self->m_multiLayerStorage.get());
            auto contractAddress = unhexAddress(contract);
            ledger::account::EVMAccount account(view, contractAddress);
            auto code = co_await ledger::account::code(account);

            if (!code)
            {
                callback(nullptr, {});
                co_return;
            }
            auto bytesView = code->get();
            callback(nullptr, bcos::bytes(bytesView.begin(), bytesView.end()));
        }(this, contract, std::move(callback)));
    }

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            auto view = fork(self->m_multiLayerStorage.get());
            auto contractAddress = unhexAddress(contract);
            ledger::account::EVMAccount account(view, contractAddress);
            auto abi = co_await ledger::account::abi(account);

            if (!abi)
            {
                callback(nullptr, {});
                co_return;
            }
            callback(nullptr, std::string(abi->get()));
        }(this, contract, std::move(callback)));
    }

    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override
    {
        callback(nullptr);
    }

    void stop() override {};

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