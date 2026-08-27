// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — SchedulerInterface for OP. Linear only: blockGasLeft, state-diff
// visibility, and deposit order forbid a parallel scheduler.
// executeBlock: preBlockOpSteps → SchedulerSerialImpl(serial=true) →
// finalizeOpBlockResult → commitment check → stash m_pending only if verify=true.
// One pending slot: commit (or same-height replace) before execute of another height —
// MLS mergeBackStorage is FIFO oldest, not the just-pushed layer.
// commitBlock: prewriteBlockToBuffer(announcedHash) → mergeBackStorage.
// Committed-tip sibling reorg (ReorgUndo / one-level rollback) is a follow-up.

#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommitments.h>
#include <opstack-executor/OpSchedulerPolicy.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/HistoricalCallStorage.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/IOServicePool.h>
#include <fmt/format.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
#define OP_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_SCHEDULER")

/// executeBlock → commitBlock payload. announcedBlockHash is the CL hash; do not
/// recompute it from executedHeader (optional fields are incomplete).
template <class MultiLayerStorage>
class OpScheduler : public scheduler::SchedulerInterface
{
public:
    using ViewType = typename MultiLayerStorage::ViewType;
    using Ptr = std::shared_ptr<OpScheduler>;

    struct PendingBlock
    {
        protocol::Block::Ptr block;                      // receipts attached at commit time
        bcos::evm::engine::OpExecuteBlockResult result;  // commitments + receipts
        bcos::crypto::HashType announcedBlockHash;       // keyed by the CL-announced hash
        protocol::BlockHeader::Ptr executedHeader;       // commitment-filled header
        bool verified = false;                           // true only after verify=true + pushView
    };

    /// execute() result before it is wrapped as PendingBlock.
    struct ExecuteOutcome
    {
        bcos::evm::engine::OpExecuteBlockResult result;
        bcos::crypto::HashType announcedBlockHash;
    };

    // ---- SchedulerInterface overrides ----

    /// Engine newPayload drives this; PBFT/sync do not.
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) override
    {
        // Capturing lambda would hold this/block/cb in the closure, which task::wait destroys at
        // the end of this full-expression; a coroutine that genuinely suspends would then read a
        // freed closure (BaselineScheduler-tpp.h uses this parameter form for the same reason).
        task::wait([](decltype(this) self, bcos::protocol::Block::Ptr block, bool verify,
                       std::function<void(
                           bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
                           callback) -> task::Task<void> {
            std::apply(callback, co_await self->coExecuteBlock(std::move(block), verify));
        }(this, std::move(block), verify, std::move(callback)));
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        task::wait(
            [](decltype(this) self, bcos::protocol::BlockHeader::Ptr header,
                std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback)
                -> task::Task<void> {
                std::apply(callback, co_await self->coCommitBlock(std::move(header)));
            }(this, std::move(header), std::move(callback)));
    }

    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }

    void reset(std::function<void(Error::Ptr)> callback) override
    {
        std::scoped_lock lock(m_executeMutex, m_commitMutex, m_pendingMutex);
        if (m_pending)
        {
            OP_SCHEDULER_LOG(INFO) << "reset: dropping uncommitted pending block "
                                   << m_pending->executedHeader->number();
            if (m_pending->verified)
            {
                m_multiLayerStorage->popFrontStorage();
            }
            m_pending.reset();
            // Continuity keys off lastExecuted; hydrateCommittedTip is a no-op once
            // lastCommitted is set. Restore the watermark to the committed tip.
            m_lastExecutedBlockNumber.store(m_lastCommittedBlockNumber.load());
        }
        callback(nullptr);
    }

    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    /// eth_call on the latest committed state. Failures return an RPC Error, not a status-0
    /// receipt.
    void call(protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait(
            [](decltype(this) self, protocol::Transaction::Ptr transaction,
                std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
                -> task::Task<void> {
                try
                {
                    callback(nullptr, co_await self->coCallLatest(std::move(transaction)));
                }
                catch (const std::exception& e)
                {
                    // Round-4 F1: no dedicated OpStorageError clause — a storage fault must
                    // classify as OpStorageFault ("storage fault") exactly like callAtBlock's
                    // catch(std::exception&) arm, so latest and historical calls report the same
                    // node-local fault identically.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING) << LOG_DESC("eth_call failed")
                                              << LOG_KV("detail", boost::diagnostic_information(e));
                    callback(BCOS_ERROR_PTR(code, fmt::format("eth_call failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        nullptr);
                }
                catch (...)
                {
                    // Same shape as callAtBlock: classify the unrecognized object and log a trace —
                    // an unlogged "unknown exception" would leave the operator nothing to
                    // correlate.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("eth_call failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        nullptr);
                }
            }(this, std::move(transaction), std::move(callback)));
    }

    /// eth_call against the committed MPT at @p blockNumber.
    void callAtBlock(protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait(
            [](decltype(this) self, protocol::Transaction::Ptr transaction,
                protocol::BlockNumber blockNumber,
                std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
                -> task::Task<void> {
                try
                {
                    auto [error, receipt] =
                        co_await self->coCallAtBlock(std::move(transaction), blockNumber);
                    callback(std::move(error), std::move(receipt));
                }
                catch (const std::exception& e)
                {
                    // Log the detail; return a generic RPC reason.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                        << LOG_KV("detail", boost::diagnostic_information(e));
                    callback(BCOS_ERROR_PTR(
                                 code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                           blockNumber, rpcSafeReason(code))),
                        nullptr);
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(
                                 code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                           blockNumber, rpcSafeReason(code))),
                        nullptr);
                }
            }(this, std::move(transaction), blockNumber, std::move(callback)));
    }

    /// Contract code at the latest committed height. Do not use getLedgerConfig (header.hash()
    /// throws).
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait(
            [](decltype(this) self, std::string contract,
                std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) -> task::Task<void> {
                try
                {
                    auto view = self->m_multiLayerStorage->forkCommitted();
                    auto blockNumber = co_await bcos::ledger::getCurrentBlockNumber(
                        view, bcos::ledger::fromStorage);
                    bcos::ledger::Features features;
                    co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                    bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                        features.get(bcos::ledger::Features::Flag::feature_raw_address));
                    auto code = co_await account.code();
                    if (!code)
                    {
                        callback(nullptr, {});
                        co_return;
                    }
                    auto bytesView = code->get();
                    callback(nullptr, bcos::bytes(bytesView.begin(), bytesView.end()));
                }
                catch (const std::exception& e)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getCode failed") << LOG_KV("detail", e.what());
                    callback(BCOS_ERROR_PTR(code, fmt::format("getCode failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getCode failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("getCode failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
            }(this, std::string(contract), std::move(callback)));
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        task::wait(
            [](decltype(this) self, std::string contract,
                std::function<void(bcos::Error::Ptr, std::string)> callback) -> task::Task<void> {
                try
                {
                    auto view = self->m_multiLayerStorage->forkCommitted();
                    auto blockNumber = co_await bcos::ledger::getCurrentBlockNumber(
                        view, bcos::ledger::fromStorage);
                    bcos::ledger::Features features;
                    co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                    bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                        features.get(bcos::ledger::Features::Flag::feature_raw_address));
                    auto abi = co_await account.abi();
                    if (!abi)
                    {
                        callback(nullptr, {});
                        co_return;
                    }
                    callback(nullptr, std::string(abi->get()));
                }
                catch (const std::exception& e)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getABI failed") << LOG_KV("detail", e.what());
                    callback(BCOS_ERROR_PTR(code, fmt::format("getABI failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getABI failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("getABI failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
            }(this, std::string(contract), std::move(callback)));
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) override
    {
        auto const addressOwned = std::string(address);
        auto const keyOwned = std::string(key);
        auto view = this->m_multiLayerStorage->fork();
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::account::EVMAccount account(
            view, addressOwned, features.get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(keyOwned);
    }

    /// Pending execute result, if any.
    std::optional<bcos::evm::engine::OpExecuteBlockResult> peekExecuteResult()
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pending)
            return std::nullopt;
        return m_pending->result;
    }

    /// ledger may be null (execute only). ioServicePool is required (SchedulerSerialImpl GC).
    OpScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkFlags forkFlags, bcos::protocol::BlockFactory::Ptr blockFactory,
        MultiLayerStorage& multiLayerStorage, bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::IOServicePool::Ptr ioServicePool)
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkFlags(forkFlags),
        m_multiLayerStorage(&multiLayerStorage),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_ioServicePool(std::move(ioServicePool))
    {
        // execute() tolerates a null ledger; commit does not (see coCommitBlock).
        // Default no-op notifiers. An empty std::function would throw inside the async task.
        m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        m_transactionNotifier = [](bcos::protocol::BlockNumber,
                                    bcos::protocol::TransactionSubmitResultsPtr,
                                    std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

    /// Optional RPC block-number callback; commitBlock invokes it after a successful merge.
    void setBlockNumberNotifier(std::function<void(bcos::protocol::BlockNumber)> notifier)
    {
        if (notifier)  // symmetric with setTransactionNotifier: an empty std::function would
        {              // throw bad_function_call inside the commit task's try block.
            m_blockNumberNotifier = std::move(notifier);
        }
    }

    /// Optional txpool eviction callback; commitBlock invokes it after a successful merge.
    void setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(bcos::Error::Ptr)>)>
            notifier)
    {
        if (notifier)
        {
            m_transactionNotifier = std::move(notifier);
        }
    }

    /// When true, execute() compares buildAndCollect against a full stateRootOf rebuild.
    /// Defaults off: the equality contract lives in IncrementalMPTRootMatchesFullRebuild.
    void setCrossCheckIncrementalRoot(bool enable) { m_crossCheckIncrementalRoot = enable; }

private:
    // ---- execute / commit ----

    task::Task<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        protocol::Block::Ptr block, bool verify)
    {
        try
        {
            auto blockHeader = block->blockHeader();
            OP_SCHEDULER_LOG(INFO)
                << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                << block->transactionsMetaDataSize() << " | " << block->transactionsSize();
            auto number = blockHeader->number();

            // Resend of the same announced hash at this height: reuse the cached header.
            if (auto cached = fastPathHit(number, *blockHeader))
            {
                co_return {nullptr, cached->first, cached->second};
            }

            // One execute at a time. Also take m_commitMutex so pushView / popFrontStorage
            // cannot race mergeBackStorage (reset() already takes all three). NOTE: these
            // std::unique_lock objects span the co_awaits below, which is safe ONLY while the
            // whole task::wait chain runs synchronously to completion on the calling thread
            // (bcos::task's symmetric transfer: Wait.h starts an AsyncTask and returns at the
            // first real suspension). If a future awaitable genuinely suspends, the closure
            // fix above (parameter-form task::wait) is not enough here — the locks must be
            // narrowed or replaced with a coroutine-aware lock.
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = std::string{"Another block is executing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }
            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }

            // One pending slot: refuse another height, replace only verify=true at this height.
            auto conflict = PendingConflict::None;
            if (number > 0)
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                if (m_pending)
                {
                    conflict = classifyPendingConflict(
                        true, m_pending->executedHeader->number(), number, verify);
                    if (conflict == PendingConflict::RefuseOtherHeight)
                    {
                        auto const pendingHeight = m_pending->executedHeader->number();
                        auto message = fmt::format(
                            "Uncommitted pending block {}; commit or replace at that height "
                            "before execute {}",
                            pendingHeight, number);
                        OP_SCHEDULER_LOG(INFO) << message;
                        co_return {BCOS_ERROR_UNIQUE_PTR(
                                       scheduler::SchedulerError::InvalidStatus, message),
                            nullptr, false};
                    }
                    if (conflict == PendingConflict::ReplaceSameHeight)
                    {
                        OP_SCHEDULER_LOG(WARNING)
                            << "Replacing uncommitted pending block " << number
                            << " with a divergent block at the same height";
                        auto const pushed = m_pending->verified;
                        m_pending.reset();
                        m_lastExecutedBlockNumber.store(number - 1);
                        if (pushed)
                        {
                            m_multiLayerStorage->popFrontStorage();
                        }
                    }
                }
            }

            co_await hydrateCommittedTip();

            // Continuity vs last executed number. A verify=false probe at the pending
            // height keeps the stash and runs on a throwaway view.
            auto const lastExecuted = m_lastExecutedBlockNumber.load();
            auto const lastCommitted = m_lastCommittedBlockNumber.load();
            bool const probeAtPending = conflict == PendingConflict::KeepProbe;
            if (number > 0 && number == lastCommitted)
            {
                auto tipView = m_multiLayerStorage->forkCommitted();
                auto const canonicalAtHeight =
                    co_await ledger::getBlockHash(tipView, number, ledger::fromStorage);
                if (canonicalAtHeight.has_value() &&
                    *canonicalAtHeight == bcos::protocol::EthBlockHeader::computeHash(*blockHeader))
                {
                    OP_SCHEDULER_LOG(INFO)
                        << "Block " << number
                        << " is already the committed canonical tip; serving without re-execution";
                    auto served = m_blockFactory->blockHeaderFactory()->populateBlockHeader(
                        protocol::BlockHeader::ConstPtr{
                            blockHeader.get(), [](protocol::BlockHeader const*) {}});
                    co_return {nullptr, std::move(served), false};
                }
                auto message = fmt::format(
                    "Block {} is a sibling of the committed tip; one-level tip reorg "
                    "is not in this scheduler slice",
                    number);
                OP_SCHEDULER_LOG(WARNING) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }
            if (lastExecuted != -1 && number - lastExecuted != 1 && !probeAtPending)
            {
                auto message =
                    fmt::format("Discontinuous execute block number! expect: {} input: {}",
                        lastExecuted + 1, number);
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }

            // Writes go to a committed-parent view. KeepProbe must not see the uncommitted
            // pending layer; after ReplaceSameHeight popFront (or with no pending) this equals
            // fork(). getPendingStorageAt still uses fork() so it can read the pending slot.
            auto view = m_multiLayerStorage->forkCommitted();
            view.newMutable();

            auto transactions = co_await getTransactions(*block, view);
            if (std::any_of(transactions.begin(), transactions.end(),
                    [](auto const& tx) { return tx == nullptr; }))
            {
                auto message =
                    fmt::format("Not found transactions in txpool for block: {}", number);
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false};
            }

            auto ledgerConfig = co_await loadLedgerConfig(view, number);

            // Persist trie nodes only on verify=true + feature_l2_ethereum_compat.
            bool const persistTrieNodes =
                verify &&
                ledgerConfig->features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
            auto outcome =
                co_await execute(view, *blockHeader, transactions, *ledgerConfig, persistTrieNodes);

            // Copy execution commitments onto a header cloned from the announced payload.
            bool sysBlock = false;
            auto executedHeader = co_await finishExecute(
                view, outcome, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // When verify=true, announced header fields must match execution.
            namespace engine = bcos::evm::engine;
            if (verify)
            {
                if (executedHeader->withdrawalsRoot().has_value() !=
                    blockHeader->withdrawalsRoot().has_value())
                {
                    throw bcos::evm::OpConsensusError(
                        "OpScheduler: commitment mismatch on field withdrawalsRoot");
                }
                if (auto mismatch = engine::mismatchedFieldOf(
                        headerCommitments(*executedHeader), headerCommitments(*blockHeader)))
                {
                    throw bcos::evm::OpConsensusError(
                        "OpScheduler: commitment mismatch on field " + *mismatch);
                }
            }

            // Push and stash only when verify is true. Probe results are returned, not committed.
            if (verify)
            {
                m_multiLayerStorage->pushView(std::move(view));
                {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    m_pending = PendingBlock{std::move(block), std::move(outcome.result),
                        outcome.announcedBlockHash, executedHeader, true};
                }
                m_lastExecutedBlockNumber.store(number);
            }

            co_return {nullptr, std::move(executedHeader), sysBlock};
        }
        catch (std::exception& e)
        {
            auto message =
                fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
        catch (...)
        {
            auto message = std::string{"Execute block failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
    }

    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        protocol::BlockHeader::Ptr header)
    {
        try
        {
            OP_SCHEDULER_LOG(INFO) << "Commit block: " << header->number();
            auto number = header->number();

            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }
            if (!m_ledger)
            {
                auto message = std::string{
                    "OpScheduler: commit requires a ledger (execute-only construction)"};
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }

            // Copy pending under the lock so later awaits do not race a replacing execute.
            PendingBlock pending;
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                if (!m_pending || !m_pending->verified ||
                    m_pending->executedHeader->number() != number)
                {
                    co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError,
                                   "Unexpected empty results!"),
                        nullptr};
                }
                pending = *m_pending;
            }

            // Bind the commit to the exact block that was executed: a replace at this height
            // swaps m_pending for a divergent block, and committing its layer under the old
            // header would silently persist a different payload than the caller believes.
            // Compare the executed headers directly — announcedBlockHash is the CL hash of the
            // announced header and cannot be recomputed from the executed header (engine wiring
            // passes the executed header back into commitBlock).
            if (bcos::protocol::EthBlockHeader::computeHash(*header) !=
                bcos::protocol::EthBlockHeader::computeHash(*pending.executedHeader))
            {
                auto message = fmt::format(
                    "Commit block {} does not match the announced block being committed", number);
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }

            if (!co_await commitContinuityCheck(number))
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                               "Commit block continuity check failed!"),
                    nullptr};
            }

            auto storage = co_await commitPersist(pending);

            // Single merge: all-or-nothing.
            co_await m_multiLayerStorage->mergeBackStorage(*storage);

            // Drop the slot only after merge succeeds, and only if it is still this block.
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                if (m_pending && m_pending->verified &&
                    m_pending->executedHeader->number() == number &&
                    m_pending->announcedBlockHash == pending.announcedBlockHash)
                {
                    m_pending.reset();
                }
            }

            auto ledgerConfig = co_await loadCommitLedgerConfig(header);
            m_lastCommittedBlockNumber.store(number);
            commitLock.unlock();

            OP_SCHEDULER_LOG(INFO) << "Commit block finished: " << number;
            notifyBlockNumber(number);

            co_return {nullptr, ledgerConfig};
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
        catch (...)
        {
            auto message = std::string{"Commit block failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
    }

    /// Hit only when height and announced hash both match a verified pending.
    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number, protocol::BlockHeader const& announcedHeader)
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pending || !m_pending->verified || m_pending->executedHeader->number() != number)
        {
            return std::nullopt;
        }
        if (m_pending->announcedBlockHash !=
            bcos::protocol::EthBlockHeader::computeHash(announcedHeader))
        {
            OP_SCHEDULER_LOG(INFO) << "Fast-path cache holds a different block at height " << number
                                   << "; ignoring cache and re-executing";
            return std::nullopt;
        }
        OP_SCHEDULER_LOG(INFO) << "Block has been executed, return result directly";
        return std::pair{m_pending->executedHeader, false};
    }

    /// OP blocks carry txs inline.
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// preBlockOpSteps → serial per-tx → finalizeOpBlockResult.
    /// persistTrieNodes: persist incremental MPT nodes when number > 0.
    task::Task<ExecuteOutcome> execute(ViewType& view, protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig, bool persistTrieNodes)
    {
        namespace op = bcos::evm::opstack;
        namespace detail = bcos::evm::engine::detail;

        // Views into each tx envelope; transactions outlive this vector.
        std::vector<bcos::bytesConstRef> rawTxBytes;
        rawTxBytes.reserve(transactions.size());
        for (auto const& tx : transactions)
        {
            rawTxBytes.emplace_back(tx->extraTransactionBytes());
        }

        bcos::evm::engine::OpExecuteBlockResult result;

        // Assigned inside the try; the catch ladder below reclassifies a poisoned slot as a
        // storage fault even when the escaping exception is not std::exception-matching
        // (wedprcrypto's corrupted typed-catch, Storage2State.h ladder comment).
        std::shared_ptr<SharedErrorSlot> sharedError;
        auto rethrowStorageFaultIfPoisoned = [&sharedError]() {
            if (!sharedError)
                return;
            std::lock_guard lock(sharedError->mutex);
            if (!sharedError->message.empty())
                throw bcos::evm::engine::OpStorageError(
                    "OpScheduler: block state read fault (poisoned): " + sharedError->message);
        };
        try
        {
            const auto& cfg = op::configAt(m_forkFlags);

            // Split deposits from other typed envelopes.
            std::vector<op::DepositTx> deposits;
            deposits.reserve(rawTxBytes.size());
            for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            {
                auto const& raw = rawTxBytes[i];
                if (raw.empty())  // empty envelope: raw[0] would be out of bounds
                    throw bcos::evm::OpConsensusError("OpScheduler: empty envelope");
                auto const typeByte = raw[0];
                if (op::classifyTxType(typeByte) == static_cast<uint8_t>(op::kDepositTxType))
                {
                    try
                    {
                        deposits.push_back(
                            OpstackExecutor::depositFromTransaction(*transactions[i]));
                    }
                    catch (const OpTxValidationFailed& e)
                    {
                        throw bcos::evm::OpConsensusError(
                            std::string("OpScheduler: malformed deposit: ") + e.what());
                    }
                }
                // Reject blob (0x03) and 0x7d type bytes.
                else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 &&
                         typeByte != 0x04)
                    throw bcos::evm::OpConsensusError(
                        fmt::format("OpScheduler: unsupported tx type byte 0x{:02x}",
                            static_cast<unsigned>(typeByte)));
            }

            bcos::ledger::LedgerConfig execLedgerConfig;
            execLedgerConfig.setEVMCRevision(cfg.rev);

            sharedError = std::make_shared<SharedErrorSlot>();
            OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

            // Block-start system call, deposit-first check, Jovian shape, DA scalar.
            std::optional<std::string> hashErr;
            std::optional<uint16_t> daFootprintGasScalar;
            std::optional<detail::RecentBlockHashes<ViewType>> hashes;
            bcos::evm::engine::preBlockOpSteps(view, header, cfg, rawTxBytes, deposits, executor,
                hashes, hashErr, daFootprintGasScalar);

            // Fee params load on the first normal tx. blockGasLeft is narrowed from gasLimit.
            OpBlockExecutionContext ctx{.fee = {},
                .blockGasLeft =
                    detail::narrowU256ToI64(header.gasLimit(), "OpScheduler blockGasLeft"),
                .blockHashes = &*hashes,
                .chainId = m_chainId,
                .daFootprintGasScalar = daFootprintGasScalar};

            // Linear per-tx loop (serial=true, chunk size 1).
            bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
                m_ioServicePool, /*chunkSize=*/1, /*serial=*/true);
            auto transactionsRefs =
                transactions |
                ::ranges::views::transform(
                    [](protocol::Transaction::ConstPtr const& ptr) -> protocol::Transaction const& {
                        return *ptr;
                    });
            auto receipts = co_await serialScheduler.executeBlock(
                view, executor, header, transactionsRefs, execLedgerConfig, ctx);

            // Finalize receipts/seal. incrementalRoot skips finalize's own full rebuild.
            // The incremental vs full equality contract is IncrementalMPTRootMatchesFullRebuild;
            // an optional debug cross-check (default off) can still run stateRootOf here.
            bool const incrementalRoot = persistTrieNodes && header.number() > 0;
            result =
                bcos::evm::engine::finalizeOpBlockResult(executor, view, header, execLedgerConfig,
                    cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr, incrementalRoot);

            // Persist this block's trie nodes. Parent nodes must already exist; otherwise
            // MPTInvariantViolation (do not rebuild from an empty trie).
            if (incrementalRoot)
            {
                bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
                auto parentBlock = co_await ledger::getBlockData(
                    view, header.number() - 1, ledger::HEADER, *m_blockFactory);
                auto const parentRoot = parentBlock->blockHeader()->stateRoot();
                try
                {
                    auto delta = co_await ledger::mpt::buildAndCollect(
                        nodeStorage, parentRoot, view, /*l2Mode=*/true);
                    if (m_crossCheckIncrementalRoot)
                    {
                        bcos::evm::evmstate::Storage2State<ViewType> fullCheck(
                            view, executor.sharedError());
                        auto const fullRoot = bcos::evm::engine::detail::toBcosH256(
                            bcos::evm::stateRootOf(fullCheck));
                        if (fullCheck.poisoned())
                        {
                            throw bcos::evm::engine::OpStorageError(
                                fmt::format("OpScheduler: full rebuild poisoned at block {}: {}",
                                    header.number(), fullCheck.firstError()));
                        }
                        if (delta.stateRoot != fullRoot)
                        {
                            throw bcos::evm::engine::OpStorageError(fmt::format(
                                "OpScheduler: incremental MPT root diverged from the full rebuild "
                                "at block {} (incremental={}, full={}, parent={})",
                                header.number(), delta.stateRoot.hexPrefixed(),
                                fullRoot.hexPrefixed(), parentRoot.hexPrefixed()));
                        }
                    }
                    result.stateRoot = delta.stateRoot;
                }
                catch (const bcos::ledger::mpt::MPTInvariantViolation& e)
                {
                    // Missing parent trie nodes: fail rather than rebuild from an empty trie.
                    throw bcos::evm::engine::OpStorageError(fmt::format(
                        "OpScheduler: incremental MPT build at block {} failed — parent block "
                        "{}'s state root {} has no persisted trie nodes (scenario B / "
                        "feature_l2_ethereum_compat must be active since genesis): {}",
                        header.number(), header.number() - 1, parentRoot.hex(), e.what()));
                }
            }
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            // A poisoned slot is a storage fault even when validation wrapped it as consensus:
            // Storage2State reads are noexcept and swallow the fault into the shared slot while
            // returning defaults, so a missing/corrupt trie row under the tx sender surfaces as
            // an insufficient-funds-style OpConsensusError (OpstackExecutor::prepare wraps
            // OpTxValidationFailed with no slot check). Same check coCallOnView runs for every
            // exception type on the eth_call path.
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (...)
        {
            rethrowStorageFaultIfPoisoned();
            throw bcos::evm::OpConsensusError(
                "OpScheduler: execute threw an unrecognized (non-std::exception) object; "
                "raw tx decode or block-level consensus fault");
        }

        // Commit keys on the announced payload hash; finishExecute() mirrors announced
        // metadata + execution commitments onto executedHeader, which stays out of this identity.
        bcos::crypto::HashType announcedBlockHash =
            bcos::protocol::EthBlockHeader::computeHash(header);
        co_return ExecuteOutcome{std::move(result), announcedBlockHash};
    }

    /// Copy execution commitments onto a clone of the announced header.
    task::Task<protocol::BlockHeader::Ptr> finishExecute(ViewType& /*view*/,
        ExecuteOutcome const& outcome, protocol::BlockHeader const& blockHeader,
        protocol::Block& /*block*/,
        std::vector<protocol::Transaction::ConstPtr> const&
        /*transactions*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool& sysBlock)
    {
        namespace detail = bcos::evm::engine::detail;
        sysBlock = false;
        auto const& opResult = outcome.result;

        auto executedBlockHeader = m_blockFactory->blockHeaderFactory()->populateBlockHeader(
            protocol::BlockHeader::ConstPtr{&blockHeader, [](protocol::BlockHeader const*) {}});
        // populateBlockHeader mirrors the 13 framework fields only; the six Ethereum metadata
        // fields are not in its copy set, so mirror them here (identity, not just commitments).
        // Optional fields follow announced presence — never invent a value the payload lacks.
        executedBlockHeader->setCoinbase(blockHeader.coinbase());
        executedBlockHeader->setGasLimit(blockHeader.gasLimit());
        executedBlockHeader->setPrevRandao(blockHeader.prevRandao());
        if (blockHeader.baseFee())
            executedBlockHeader->setBaseFee(*blockHeader.baseFee());
        if (blockHeader.excessBlobGas())
            executedBlockHeader->setExcessBlobGas(*blockHeader.excessBlobGas());
        if (blockHeader.parentBeaconBlockRoot())
            executedBlockHeader->setParentBeaconBlockRoot(*blockHeader.parentBeaconBlockRoot());
        executedBlockHeader->setStateRoot(opResult.stateRoot);
        executedBlockHeader->setTxsRoot(opResult.txRoot);
        executedBlockHeader->setReceiptsRoot(detail::toBcosH256(opResult.seal.receiptsRoot));
        executedBlockHeader->setGasUsed(bcos::u256(opResult.gasUsed));
        auto const& bloom = opResult.seal.logsBloom;
        executedBlockHeader->setLogsBloom(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes)));
        executedBlockHeader->setWithdrawalsRoot(detail::toBcosH256(opResult.seal.withdrawalsRoot));
        if (opResult.seal.requestsHash.has_value())
            executedBlockHeader->setRequestsHash(detail::toBcosH256(*opResult.seal.requestsHash));
        if (opResult.seal.blobGasUsed.has_value())
            executedBlockHeader->setBlobGasUsed(bcos::u256(*opResult.seal.blobGasUsed));
        else if (auto const announced = blockHeader.blobGasUsed())
        {
            // Seal omitted blobGasUsed (pre-Jovian): the OP spec fixes the field at 0 here, so a
            // non-zero announcement is an invalid payload. Reject it instead of copying — the
            // copy feeds headerCommitments, and comparing the announced value against its own
            // copy makes that verify arm self-referential (always pass).
            if (*announced != bcos::u256{0})
                throw bcos::evm::OpConsensusError(
                    "OpScheduler: pre-Jovian payload must announce blobGasUsed=0");
            // Spec-valid zero: keep the identity back-fill so the header's shape stays faithful.
            executedBlockHeader->setBlobGasUsed(*announced);
        }
        co_return executedBlockHeader;
    }

    /// prewriteBlockToBuffer(announcedHash). Undo journal is a follow-up.
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commitPersist(
        PendingBlock const& pending)
    {
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        // Receipt count must equal tx count.
        if (pending.result.receipts.size() != pending.block->transactionsSize())
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a receipt count differing "
                                      "from the transaction count"});

        auto block = pending.block;
        // Idempotency: retrying a failed commit re-appends the same pending result; clear first.
        block->clearReceipts();
        for (auto const& r : pending.result.receipts)
            block->appendReceipt(r);
        // Only toShared() copies should carry setStoreToBackend.
        for (auto const& tx : block->transactions())
            tx->setStoreToBackend(false);

        // toShared() is required for ConstPtr.
        auto blockTxs = std::make_shared<protocol::ConstTransactions>(
            block->transactions() | ::ranges::views::transform([](auto tx) {
                return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
            }) |
            ::ranges::to<std::vector>());

        co_await bcos::ledger::prewriteBlockToBuffer(*m_ledger, blockTxs, block, *storage,
            pending.announcedBlockHash, /*writeNonces=*/false);
        co_return storage;
    }

    /// Load lastCommitted (and lastExecuted if still unset) from storage after restart.
    task::Task<void> hydrateCommittedTip()
    {
        if (m_lastCommittedBlockNumber.load() != -1)
        {
            co_return;
        }
        auto view = m_multiLayerStorage->forkCommitted();
        auto const tip =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        if (tip != -1)
        {
            m_lastCommittedBlockNumber.store(tip);
            if (m_lastExecutedBlockNumber.load() == -1)
            {
                m_lastExecutedBlockNumber.store(tip);
            }
        }
    }

    /// Reject already-committed or discontinuous heights.
    task::Task<bool> commitContinuityCheck(protocol::BlockNumber number)
    {
        if (!isSysContractDeploy(number))
        {
            auto lastCommitted = m_lastCommittedBlockNumber.load();
            if (lastCommitted == -1)
            {
                auto view = m_multiLayerStorage->forkCommitted();
                lastCommitted =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                m_lastCommittedBlockNumber.store(lastCommitted);
            }
            if (lastCommitted != -1 && number <= lastCommitted)
            {
                OP_SCHEDULER_LOG(INFO)
                    << "Block already committed: " << number << "! latest: " << lastCommitted;
                co_return false;
            }
            else if (lastCommitted != -1 && number - lastCommitted != 1)
            {
                OP_SCHEDULER_LOG(INFO) << "Discontinuous commit block number: " << number
                                       << "! expect: " << (lastCommitted + 1);
                co_return false;
            }
        }
        co_return true;
    }

    /// Invoke the installed notifiers (ctor defaults are no-ops).
    void notifyBlockNumber(protocol::BlockNumber number)
    {
        m_blockNumberNotifier(number);
        m_transactionNotifier(number, std::make_shared<bcos::protocol::TransactionSubmitResults>(),
            [](const Error::Ptr&) {});
    }

    /// Build LedgerConfig without header.hash() (throws on an OP header).
    task::Task<ledger::LedgerConfig::Ptr> loadLedgerConfig(
        ViewType& view, protocol::BlockNumber number)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(number);
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        ledgerConfig->setFeatures(features);
        co_return ledgerConfig;
    }

    /// Commit-path LedgerConfig (number + timestamp only).
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

public:
    /// Stable RPC reason text for a classified scheduler code.
    static constexpr std::string_view rpcSafeReason(scheduler::SchedulerError code) noexcept
    {
        return code == scheduler::SchedulerError::OpStorageFault      ? "storage fault" :
               code == scheduler::SchedulerError::OpConsensusRejected ? "consensus rejection" :
                                                                        "internal error";
    }

    /// Map OP / MPT exceptions to SchedulerError. Raw MPT faults are storage faults.
    scheduler::SchedulerError classifyException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            return scheduler::SchedulerError::OpConsensusRejected;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (const bcos::ledger::mpt::MPTInvariantViolation&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (const bcos::ledger::mpt::MPTDecodeError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (...)
        {
            return scheduler::SchedulerError::UnknownError;
        }
    }

    /// Recover what() from known exception types; unknown throws stay generic.
    std::string describeException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::OpConsensusError& e)
        {
            return e.what();
        }
        catch (const bcos::evm::engine::OpStorageError& e)
        {
            return e.what();
        }
        catch (const bcos::ledger::mpt::MPTInvariantViolation& e)
        {
            return e.what();
        }
        catch (const bcos::ledger::mpt::MPTDecodeError& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unclassified exception (not derived from std::exception)";
        }
    }

private:
    /// Commitment fields used to compare executed vs announced headers.
    static bcos::evm::engine::OpBlockCommitments headerCommitments(protocol::BlockHeader const& h)
    {
        namespace detail = bcos::evm::engine::detail;
        auto bloom = h.logsBloom();
        bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
        std::optional<uint64_t> blobGasUsed;
        if (auto bg = h.blobGasUsed())
            blobGasUsed = detail::narrowU256ToU64(*bg, "headerCommitments blobGasUsed");
        return bcos::evm::engine::OpBlockCommitments{
            .receiptsRoot = h.receiptsRoot(),
            .logsBloom = logsBloom,
            .withdrawalsRoot = h.withdrawalsRoot().value_or(bcos::h256{}),
            .stateRoot = h.stateRoot(),
            .gasUsed = h.gasUsed(),
            .txRoot = h.txsRoot(),
            .blobGasUsed = blobGasUsed,
            .requestsHash = h.requestsHash(),
        };
    }

    /// Strict hex-address parse for getCode/getABI.
    static evmc_address parseAddress(std::string_view view)
    {
        evmc_address out{};
        if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
            view.remove_prefix(2);
        if (view.size() != sizeof(out.bytes) * 2)
            throw std::invalid_argument("OpScheduler: invalid address (need 40 hex chars)");
        boost::algorithm::unhex(view.begin(), view.end(), out.bytes);
        return out;
    }

    /// Dry-run one eth_call on @p view. errTag prefixes errors.
    template <class AnyView>
    task::Task<protocol::TransactionReceipt::Ptr> coCallOnView(AnyView& view,
        protocol::BlockHeader const& header, protocol::Transaction const& transaction,
        bcos::ledger::LedgerConfig const& ledgerConfig, std::string_view errTag)
    {
        namespace op = bcos::evm::opstack;
        namespace detail = bcos::evm::engine::detail;

        const auto& cfg = op::configAt(m_forkFlags);
        bcos::evm::evmstate::Storage2State<AnyView> stateView(view);
        auto fee = op::loadOpFeeParams(stateView);
        // Fail if Storage2State poisoned the fee-param read.
        if (stateView.poisoned())
            throw bcos::evm::engine::OpStorageError(fmt::format(
                "OpScheduler: {} fee-param read fault: {}", errTag, stateView.firstError()));
        const auto blockGasLeft =
            detail::narrowU256ToI64(header.gasLimit(), "OpScheduler blockGasLeft");

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<AnyView> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // One executor (and one evmc::VM) per call.
        auto sharedError = std::make_shared<SharedErrorSlot>();
        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

        auto takeSharedError = [&]() {
            std::lock_guard lock(sharedError->mutex);
            return sharedError->message;
        };

        protocol::TransactionReceipt::Ptr receipt;
        try
        {
            receipt = co_await executor.executeTransaction(view, header, transaction,
                /*contextID=*/0, ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId,
                &hashes);
        }
        catch (...)
        {
            // A poisoned slot is a storage fault even if validation wrapped it as consensus
            // (missing inner node → get_account returns nullopt → insufficient funds).
            if (auto firstError = takeSharedError(); !firstError.empty())
                throw bcos::evm::engine::OpStorageError(
                    fmt::format("OpScheduler: {} state read fault: {}", errTag, firstError));
            throw;
        }

        // Fail if the executor reported a storage read fault.
        if (auto firstError = takeSharedError(); !firstError.empty())
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} state read fault: {}", errTag, firstError));
        if (hashErr.has_value())
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} block-hash lookup failed: {}", errTag, *hashErr));
        co_return receipt;
    }

    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;

        auto view = m_multiLayerStorage->forkCommitted();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, *m_blockFactory);
        // blockHeader() returns a shared_ptr by value; keep it alive.
        auto blockHeader = block->blockHeader();
        auto const& header = *blockHeader;

        const auto& cfg = op::configAt(m_forkFlags);

        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(blockNumber);
        ledgerConfig->setTimestamp(header.timestamp());
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, blockNumber);
        ledgerConfig->setFeatures(features);
        ledgerConfig->setEVMCRevision(cfg.rev);

        co_return co_await coCallOnView(view, header, *transaction, *ledgerConfig, "call");
    }

    /// eth_call against the committed MPT at @p blockNumber. Refusals return Error, not throw.
    task::Task<std::tuple<Error::Ptr, protocol::TransactionReceipt::Ptr>> coCallAtBlock(
        protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber)
    {
        namespace op = bcos::evm::opstack;

        auto latestView = m_multiLayerStorage->forkCommitted();
        auto latestNumber =
            co_await bcos::ledger::getCurrentBlockNumber(latestView, bcos::ledger::fromStorage);
        // Negative or beyond-latest: InvalidBlockNumber.
        if (blockNumber < 0 || blockNumber > latestNumber)
        {
            co_return std::tuple{
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                    fmt::format("eth_call: block {} does not exist (latest: {})", blockNumber,
                        latestNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }
        if (blockNumber == latestNumber)
        {
            // Latest height: reuse coCallLatest.
            co_return std::tuple{
                Error::Ptr{nullptr}, co_await coCallLatest(std::move(transaction))};
        }

        // Historical call needs feature_l2_ethereum_compat (full-fidelity MPT).
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, latestView, blockNumber);
        if (!features.get(bcos::ledger::Features::Flag::feature_l2_ethereum_compat))
        {
            co_return std::tuple{
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                    fmt::format("eth_call: historical call at block {} requires the "
                                "full-fidelity MPT of an L2 Ethereum-compat chain "
                                "(feature_l2_ethereum_compat, scenario B)",
                        blockNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }

        auto block = co_await bcos::ledger::getBlockData(
            latestView, blockNumber, bcos::ledger::HEADER, *m_blockFactory);
        // blockHeader() returns a shared_ptr by value; keep it alive.
        auto blockHeader = block->blockHeader();
        auto const& header = *blockHeader;
        auto const stateRoot = header.stateRoot();
        if (stateRoot == bcos::crypto::HashType{})
        {
            co_return std::tuple{BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                                     fmt::format("eth_call: no MPT state root recorded in block "
                                                 "{}'s header",
                                         blockNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }
        // Empty root needs no nodes; any other missing root is an error.
        if (stateRoot != bcos::ledger::mpt::emptyRootHash() &&
            !co_await storage2::existsOne(latestView, storage2::mptNodeStateKey(stateRoot)))
        {
            co_return std::tuple{
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                    fmt::format("eth_call: block {}'s state root has no persisted MPT nodes "
                                "(trie-node persistence was not yet active at that height)",
                        blockNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }

        const auto& cfg = op::configAt(m_forkFlags);

        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(blockNumber);
        ledgerConfig->setTimestamp(header.timestamp());
        ledgerConfig->setFeatures(features);
        ledgerConfig->setEVMCRevision(cfg.rev);

        // Fresh mutable layer over the historical MPT; call writes are not persisted.
        using HistoricalBackend = bcos::scheduler_v1::HistoricalStateBackend<ViewType>;
        HistoricalBackend historicalBackend(latestView, stateRoot);
        storage2::View<typename MultiLayerStorage::MutableStorage, void, HistoricalBackend>
            historicalView(std::addressof(historicalBackend));
        historicalView.newMutable();
        co_return std::tuple{Error::Ptr{nullptr}, co_await coCallOnView(historicalView, header,
                                                      *transaction, *ledgerConfig, "historical")};
    }

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkFlags m_forkFlags;

    MultiLayerStorage* m_multiLayerStorage = nullptr;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::IOServicePool::Ptr m_ioServicePool;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(bcos::Error::Ptr)>)>
        m_transactionNotifier;
    bool m_crossCheckIncrementalRoot = false;
    std::mutex m_executeMutex;
    std::atomic<int64_t> m_lastExecutedBlockNumber{-1};
    std::mutex m_commitMutex;
    std::atomic<int64_t> m_lastCommittedBlockNumber{-1};
    std::mutex m_pendingMutex;
    std::optional<PendingBlock> m_pending;
};


}  // namespace bcos::executor_v1::opstack
