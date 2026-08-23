// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — the OP-specific SchedulerInterface implementation. Standalone: no shared
// SchedulerSkeleton, so this class owns its execute/commit orchestration inline.
//
// DESIGN INVARIANT — OP is LINEAR-ONLY (never a parallel/partitioned scheduler). OP block
// execution carries three cross-transaction sequential dependencies that forbid any
// parallel or chunked-staged execution model:
//   1. the blockGasLeft pool — each tx validates against the gas left AFTER prior txs ran;
//   2. state-diff visibility — each tx reads the state written by the previous tx;
//   3. deposit ordering — L1 attributes are injected strictly in order.
// runOpBlockInjection (the linear per-tx executor) was retired in Task 5: the per-tx loop is the
// shared SchedulerSerialImpl(serial=true) (grain-size 1 — the same degenerate loop, now shared),
// the block-pre steps live in OpBlockExecute.h's preBlockOpSteps, the block-post steps in
// finalizeOpBlockResult.
// SchedulerParallelImpl must never drive OP. The three sequential dependencies above forbid any
// parallel or chunked-staged execution model.
//
// executeBlock → (preBlockOpSteps → SchedulerSerialImpl per-tx → finalizeOpBlockResult) →
// six-way verify → stash m_pending;
// commitBlock → prewriteBlockToBuffer(announcedHash) → mergeBackStorage. OP execution is
// synchronous single-block — the engine holds x_state across executeBlock→commitBlock
// (EngineServiceImpl runOpNewPayloadSteps), so a single m_pending slot survives the two
// calls (no pipelining deque needed; that is an ethereum concern).

#include <opstack-executor/OpBlockExecute.h>  // preBlockOpSteps / finalizeOpBlockResult / OpBlockSeal
#include <opstack-executor/OpCommitments.h>  // OpBlockCommitments / mismatchedFieldOf / toBcosH256
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>  // SchedulerError (OpConsensusRejected...)
#include <bcos-framework/engine/Errors.h>  // OpExecutionInternalError (commit-hook guard)
#include <bcos-framework/executor/PrecompiledTypeDef.h>  // isSysContractDeploy
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>  // readFromStorage
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>  // ConstTransactions
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>  // TransactionSubmitResults(Ptr)
#include <bcos-framework/storage2/MultiLayerStorage.h>  // storage2::View (historical call stack)
#include <bcos-framework/storage2/Storage.h>  // storage2::existsOne (M5 root-existence probe)
#include <bcos-ledger/LedgerMethods.h>        // getCurrentBlockNumber / getBlockData CPO tag_invoke
#include <bcos-ledger/mpt/Constants.h>        // emptyRootHash (empty-trie exemption, M5 probe)
#include <bcos-ledger/mpt/Errors.h>  // MPTInvariantViolation / MPTDecodeError (classifyException)
#include <bcos-ledger/mpt/MPTBuilder.h>  // buildAndCollect (①a incremental MPT)
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-storage/KeyPrefixes.h>  // storage2::mptNodeStateKey (M5 root-existence probe)
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/HistoricalCallStorage.h>  // HistoricalStateBackend (①b/③)
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>    // serial per-tx scheduler (Task 4)
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/IOServicePool.h>  // IOServicePool::Ptr (Task 4 ctor param)
#include <fmt/format.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
#define OP_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_SCHEDULER")

/// OP block execution payload carried from executeBlock to commitBlock: the execution view (which
/// the commit merges), the announced block, the six-commitment result, the CL-announced block hash
/// (never recomputed on the executed header — its optional fields are incomplete → would throw),
/// and the executed header (commit's number match + fast-path cache).
template <class MultiLayerStorage>
class OpScheduler : public scheduler::SchedulerInterface
{
public:
    using ViewType = typename MultiLayerStorage::ViewType;
    using Ptr = std::shared_ptr<OpScheduler>;

    struct PendingBlock
    {
        protocol::Block::Ptr block;                      // receipts attached at commit time
        bcos::evm::engine::OpExecuteBlockResult result;  // six commitments + receipts
        bcos::crypto::HashType announcedBlockHash;       // keyed by the CL-announced hash
        protocol::BlockHeader::Ptr executedHeader;       // commitment-filled header
    };

    /// What the execute phase produces before being wrapped into a PendingBlock.
    struct ExecuteOutcome
    {
        bcos::evm::engine::OpExecuteBlockResult result;
        bcos::crypto::HashType announcedBlockHash;
    };

public:
    // ---- SchedulerInterface overrides ----

    /// by pbft & sync — here: driven by the engine's OP newPayload (m_delegate).
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) override
    {
        task::wait([this, block = std::move(block), verify,
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            std::apply(cb, co_await coExecuteBlock(std::move(block), verify));
        }());
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        task::wait([this, header = std::move(header),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            std::apply(cb, co_await coCommitBlock(std::move(header)));
        }());
    }

    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }

    void reset(std::function<void(Error::Ptr)> callback) override
    {
        // Drop an uncommitted pending block (Tier-2: an abandoned self-built payload) so the
        // next executeBlock is not refused by the continuity guard. The pushed execute view
        // stays on the MLS stack (no pop API); abandoning is a leak bounded by the engine's
        // payload-cache eviction cadence — documented Phase A caveat.
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (m_pending)
        {
            OP_SCHEDULER_LOG(INFO) << "reset: dropping uncommitted pending block "
                                   << m_pending->executedHeader->number();
            m_pending.reset();
            m_lastExecutedBlockNumber = -1;
        }
        callback(nullptr);
    }

    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    /// eth_call: coCallLatest with injection + a hand-built LedgerConfig + double catch.
    /// Errors go back via the callback as a JSON-RPC Error, never a status-0 receipt.
    void call(protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([this, tx = std::move(transaction),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            try
            {
                cb(nullptr, co_await coCallLatest(std::move(tx)));
            }
            catch (const bcos::evm::engine::OpStorageError& e)
            {
                // Storage faults stay generic (round-2 F4: schema/corruption detail is internal —
                // full diagnostic to the node log, "internal error" wording to the RPC client;
                // CallLatestStorageReadFaultFailsLoudly pins this). Validation/user errors keep
                // their reason via e.what() in the catch below (CallInvalidReturnsError pins
                // that). callAtBlock is different — its refusals are semantic codes
                // (classifyException + rpcSafeReason).
                OP_SCHEDULER_LOG(WARNING)
                    << LOG_DESC("eth_call failed (storage fault)") << LOG_KV("detail", e.what());
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "eth_call failed: internal error (see node log)"),
                    nullptr);
            }
            catch (const std::exception& e)
            {
                // The message (e.what()) is part of the latest-call API surface: validation
                // reasons ("max fee per gas less than block base fee"...) must survive to the
                // callback (CallInvalidReturnsError).
                OP_SCHEDULER_LOG(WARNING) << LOG_DESC("eth_call failed")
                                          << LOG_KV("detail", boost::diagnostic_information(e));
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
            catch (...)
            {
                // Defense-in-depth backstop. Historically this caught the duplicate-typeinfo
                // poisoning described at describeException() below; with the evmone port fixed
                // (RTTI enabled, ports/evmone) typed catches bind again and this stays silent.
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "OpScheduler::call: unknown exception"),
                    nullptr);
            }
        }());
    }

    /// eth_call pinned at @p blockNumber (OP historical call): the boundary checks and
    /// refusals of BaselineScheduler::callAtBlock (same codes and boundary order; the
    /// feature-gate message text is slightly shorter — see coCallAtBlock), then OP-semantics
    /// execution on the block-N MPT via HistoricalStateBackend (coCallAtBlock). Requires the
    /// block's trie nodes to be persisted (①a: OP finalize's incremental build flushes them
    /// when feature_l2_ethereum_compat is on; genesis nodes come from Ledger::buildGenesisBlock).
    void callAtBlock(protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([this, tx = std::move(transaction), blockNumber,
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            try
            {
                auto [error, receipt] = co_await coCallAtBlock(std::move(tx), blockNumber);
                cb(std::move(error), std::move(receipt));
            }
            catch (const std::exception& e)
            {
                // classifyException (not a hardcoded UnknownError): OpConsensusError /
                // OpStorageError / mpt read-path faults keep their semantic codes
                // (OpScheduler.h classifyException). RPC hygiene (round-2 F4): the full
                // diagnostic goes to the node log, the RPC-bound message stays generic.
                auto const code = classifyException(std::current_exception());
                OP_SCHEDULER_LOG(WARNING)
                    << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                    << LOG_KV("detail", boost::diagnostic_information(e));
                cb(BCOS_ERROR_PTR(
                       code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                 blockNumber, rpcSafeReason(code))),
                    nullptr);
            }
            catch (...)
            {
                // Same typed-catch bypass backstop as call() (wedprcrypto-linked binaries — see
                // call()'s catch(...) comment): exceptions escape the typed catch; normalize
                // through describeException/classifyException. The RPC-bound message is the SAME
                // rpcSafeReason wording as the typed catch — which arm fires is unreliable, so
                // the observable text must not depend on it.
                auto const code = classifyException(std::current_exception());
                OP_SCHEDULER_LOG(WARNING)
                    << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                    << LOG_KV("detail", describeException(std::current_exception()));
                cb(BCOS_ERROR_PTR(
                       code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                 blockNumber, rpcSafeReason(code))),
                    nullptr);
            }
        }());
    }

    /// getCode: storage read via the readFromStorage pattern (not getLedgerConfig — header.hash()
    /// throws EmptyBlockHeaderHash for OP headers).
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
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
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
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
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) override
    {
        auto view = this->m_multiLayerStorage->fork();
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::account::EVMAccount account(
            view, address, features.get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    /// Test observation surface: returns the raw execution result of the pending block.
    std::optional<bcos::evm::engine::OpExecuteBlockResult> peekExecuteResult()
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pending)
            return std::nullopt;
        return m_pending->result;
    }

    /// Both ledger and ioServicePool are required (no defaults — a defaulted param cannot precede a
    /// non-defaulted one, and the compiler must force every construction site to pass the pool:
    /// SchedulerSerialImpl's GC defers context destruction onto it, a null pool would crash on the
    /// first deferred collect). ledger may still be nullptr explicitly (execute tolerates it).
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
        m_blockFactory(blockFactory.get()),
        m_ioServicePool(std::move(ioServicePool))
    {
        // A null ledger is tolerated by the execute path but committing would throw a null deref.
        if (ledger)
        {
            m_ledger = ledger.get();
        }
        // OP has no RPC push needs — register no-op notifiers (a default-empty std::function would
        // throw bad_function_call inside an async task → terminate), overridable by the
        // composition root.
        m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        m_transactionNotifier = [](bcos::protocol::BlockNumber,
                                    bcos::protocol::TransactionSubmitResultsPtr,
                                    std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

    /// RPC block-number push channel (alignment plan problem 3): the composition root
    /// (Initializer's m_setOpSchedulerBlockNumberNotifier) installs the callback; commitBlock
    /// fires it via notifyBlockNumber after the merge of a VALID OP block. Without the setter the
    /// ctor no-op is permanent and RPC block-number subscribers never see OP blocks.
    void setBlockNumberNotifier(std::function<void(bcos::protocol::BlockNumber)> notifier)
    {
        m_blockNumberNotifier = std::move(notifier);
    }

private:
    // ================================================================
    // Orchestration (inlined from the former SchedulerSkeleton; trimmed — OP is synchronous
    // single-block, so no pipelining deque, no backpressure, no MPT observer).
    // ================================================================

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

            // fast-path: a pending block at the same height whose announced hash matches the
            // incoming header is a resend (e.g. after a failed commit) — serve the cached header
            // without re-executing.
            if (auto cached = fastPathHit(number, *blockHeader))
            {
                co_return {nullptr, cached->first, cached->second};
            }

            // execute serialization (the engine holds x_state, but the delegate must be safe on
            // its own).
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = std::string{"Another block is executing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }

            // execute continuity (in-lock).
            if (m_lastExecutedBlockNumber != -1 && number - m_lastExecutedBlockNumber != 1)
            {
                auto message =
                    fmt::format("Discontinuous execute block number! expect: {} input: {}",
                        m_lastExecutedBlockNumber + 1, number);
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }

            // execute writes land in the view's mutable layer; commit's mergeBackStorage persists.
            auto view = m_multiLayerStorage->fork();
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

            // ①b: persist trie nodes only on the canonical pass (verify=false probe views are
            // discarded — flushing them would be wasted work) and only on scenario-B chains
            // (feature_l2_ethereum_compat), where the MPT is the complete state and
            // callAtBlock serves historical eth_call from it.
            bool const persistTrieNodes =
                verify &&
                ledgerConfig->features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
            auto outcome =
                co_await execute(view, *blockHeader, transactions, *ledgerConfig, persistTrieNodes);

            // finish: write the OP commitments into the executedHeader (skip MPT; never call
            // BlockHeader::hash() — the header is rebuilt from the announced payload).
            bool sysBlock = false;
            auto executedHeader = co_await finishExecute(
                view, outcome, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // Six-way commitment comparison (a mismatch throws OpConsensusError →
            // OpConsensusRejected), gated on `verify`: an external payload's announced
            // commitments must match execution. A SELF-BUILT payload (Tier-2 attribute-driven
            // building) executes with verify=false — its announced roots are provisional
            // zeros; the engine fills the real commitments from `executedHeader` afterwards,
            // and the payload is only published after the round-trip is self-consistent.
            namespace engine = bcos::evm::engine;
            if (verify)
            {
                if (auto mismatch = engine::mismatchedFieldOf(
                        headerCommitments(*executedHeader), headerCommitments(*blockHeader)))
                {
                    throw engine::OpConsensusError(
                        "OpScheduler: six-way commitment mismatch on field " + *mismatch);
                }
            }

            // Push the execute view onto the MLS layer stack (mergeBackStorage in the commit
            // phase requires a non-empty stack — the pushed view IS the block's state delta).
            // Probe executions (verify=false, the Tier-2 build's first pass) skip the push:
            // their view is discarded, so no layer residue accumulates per built block; the
            // canonical second pass (verify=true) pushes the view the commit merges.
            if (verify)
            {
                m_multiLayerStorage->pushView(std::move(view));
            }

            // stash for the commit phase (the view already rides the layer stack).
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending = PendingBlock{std::move(block), std::move(outcome.result),
                    outcome.announcedBlockHash, executedHeader};
            }
            m_lastExecutedBlockNumber = number;

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
            // Backstop for non-std::exception throws. Historically this also caught the
            // duplicate-typeinfo poisoning (see describeException below); with the evmone
            // port fixed (RTTI enabled, ports/evmone) std::exception types bind in the
            // catch above, and this stays silent for them.
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

            // head-advance guard: prewriteBlockToBuffer writes SYS_CURRENT_STATE unconditionally,
            // so reject already-committed / discontinuous commits (reads the storage view, not the
            // ledger's own m_stateStorage).
            if (!co_await commitContinuityCheck(number))
            {
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                               "Commit block continuity check failed!"),
                    nullptr};
            }

            // Validate the pending slot (kept in place until the merge succeeds — the original
            // skeleton popped only after merge, so a failed commit retries from the same data; a
            // stale slot at a different height is refused). Snapshot the WHOLE PendingBlock under
            // the lock: block/executedHeader are shared_ptr (refcount-safe copies) and result
            // holds vector<Receipt::Ptr>, so the copy keeps every reference alive across the
            // awaits below even if a concurrent executeBlock replaces m_pending (TOCTOU UAF).
            PendingBlock pending;
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                if (!m_pending || m_pending->executedHeader->number() != number)
                {
                    co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError,
                                   "Unexpected empty results!"),
                        nullptr};
                }
                pending = *m_pending;
            }

            auto storage = co_await commitPersist(pending);

            // The one merge (atomic: either all visible or none).
            co_await m_multiLayerStorage->mergeBackStorage(*storage);

            // Clear the slot only after the merge succeeds.
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending.reset();
            }

            auto ledgerConfig = co_await loadCommitLedgerConfig(header);
            m_lastCommittedBlockNumber = number;
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

    /// Fast-path cache: a pending block at the same height whose announced hash equals the
    /// incoming header's EthBlockHeader hash is a resend (e.g. after a failed commit) — serve the
    /// cached executedHeader without re-executing. Block number alone is not unique for an OP
    /// block: a resend carrying a DIFFERENT block at the same height must not hit the stale
    /// header, or the new payload would be reported VALID without execution — forking the chain.
    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number, protocol::BlockHeader const& announcedHeader)
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pending || m_pending->executedHeader->number() != number)
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

    // ---- hooks (kept as private helpers) ----

    /// ① Transaction source: block.transactions() → Transaction::ConstPtr (OP blocks carry
    /// inline transactions, no txpool). extraTransactionBytes holds the full envelope.
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// ② Execution kernel: three-phase — ① pre-block (system_call + deposit-first + Jovian shape +
    /// DA scalar) → ② SchedulerSerialImpl(serial=true) per-tx → ③ finalizeOpBlockResult. rawTxBytes
    /// = each tx's extraTransactionBytes; deposits = decoded 0x7E envelopes; cfg =
    /// configAt(m_forkFlags) (feature_op_jovian); executor = a per-block OpstackExecutor (one
    /// evmc::VM);
    /// ledgerConfig only needs evmcRevision.
    /// @p persistTrieNodes (①a historical eth_call): when set (and number > 0), the stateRoot
    /// comes from the incremental buildAndCollect over this block's delta (the full two-layer
    /// rebuild is skipped) and its new trie nodes land as "/mpt/" rows in this block's mutable
    /// layer — keeping the committed stateRoot resolvable by HistoricalStateBackend at any
    /// later height. Block 0 is never executed by OP; genesis nodes are persisted by
    /// Ledger::buildGenesisBlock, so no fallback branch exists here.
    task::Task<ExecuteOutcome> execute(ViewType& view, protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig, bool persistTrieNodes)
    {
        namespace op = bcos::evm::opstack;
        namespace detail = bcos::evm::engine::detail;

        // Views into each tx's live envelope: extraTransactionBytes() returns a bytesConstRef into
        // the tars Transaction (transactions outlive rawTxBytes — both are consumed within this
        // execute() scope). One view vector + N*16B view copies replaces N per-envelope heap
        // allocations.
        std::vector<bcos::bytesConstRef> rawTxBytes;
        rawTxBytes.reserve(transactions.size());
        for (auto const& tx : transactions)
        {
            rawTxBytes.emplace_back(tx->extraTransactionBytes());
        }

        bcos::evm::engine::OpExecuteBlockResult result;
        try
        {
            const auto& cfg = op::configAt(m_forkFlags);

            // Classify by type byte: deposits come from the block's Transaction objects
            // (depositFromTransaction); deposit canonicality is backed by its width checks +
            // the engine step-2 blockHash check.
            std::vector<op::DepositTx> deposits;
            deposits.reserve(rawTxBytes.size());
            for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            {
                auto const& raw = rawTxBytes[i];
                if (raw.empty())  // empty envelope: raw[0] would be out of bounds
                    throw bcos::evm::engine::OpConsensusError("OpScheduler: empty envelope");
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
                        throw bcos::evm::engine::OpConsensusError(
                            std::string("OpScheduler: malformed deposit: ") + e.what());
                    }
                }
                // NOTE (independent review #5429, finding C): this whitelist {0x7e, 0x01, 0x02,
                // 0x04, >=0xc0} is deliberately STRICTER than op-geth: decodeTyped
                // (transaction.go:218-228) also accepts 0x03 (blob) and 0x7d (post-exec) and the
                // state processor would execute them. A payload carrying either is accepted by
                // op-geth but rejected INVALID here — an intentional acceptance divergence (OP L2
                // has no blob/post-exec txs), NOT parity. op-geth has no "blob ban" check (the
                // only related gate, Jovian CalcDAFootprint, requires txs[0] to be *a* deposit);
                // the "L2 forbids blob" wording elsewhere is FISCO's own policy. DIVERGENCES.md
                // row corrected from "等价" to match.
                else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 &&
                         typeByte != 0x04)
                    throw bcos::evm::engine::OpConsensusError(
                        fmt::format("OpScheduler: unsupported tx type byte 0x{:02x}",
                            static_cast<unsigned>(typeByte)));
            }

            bcos::ledger::LedgerConfig execLedgerConfig;
            execLedgerConfig.setEVMCRevision(cfg.rev);

            auto sharedError = std::make_shared<SharedErrorSlot>();
            OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

            // ① Block-pre steps (preBlockOpSteps, OpBlockExecute.h — the single home shared with
            // the retired runOpBlockInjection): recent-block-hashes construction,
            // system_call_block_start
            // + applyStateDiff, deposit-first content check + Jovian shape, DA footprint gas
            // scalar.
            std::optional<std::string> hashErr;
            std::optional<uint16_t> daFootprintGasScalar;
            std::optional<detail::RecentBlockHashes<ViewType>> hashes;
            bcos::evm::engine::preBlockOpSteps(view, header, cfg, rawTxBytes, deposits, executor,
                hashes, hashErr, daFootprintGasScalar);

            // ② Per-block context (fee NOT loaded here — lazily on the first NORMAL tx's prepare).
            // blockGasLeft via narrowU256ToU64 (silent-truncation guard, same as coCallOnView).
            OpBlockExecutionContext ctx{.fee = {},
                .blockGasLeft = static_cast<int64_t>(
                    detail::narrowU256ToU64(header.gasLimit(), "OpScheduler blockGasLeft")),
                .blockHashes = &*hashes,
                .chainId = m_chainId,
                .daFootprintGasScalar = daFootprintGasScalar};

            // ③ Per-tx execution via the shared serial scheduler (one per block; serial=true
            // forces grain size 1 — OP is linear-only, see the class header's DESIGN INVARIANT).
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

            // ④ Block-post finalize (hashErr check lives inside finalizeOpBlockResult).
            // ①a: on the canonical scenario-B pass past genesis, the full two-layer rebuild is
            // SKIPPED — the incremental buildAndCollect in ⑤ computes the same root from the
            // block delta alone (gate: OpSchedulerTest.IncrementalMPTRootMatchesFullRebuild).
            bool const incrementalRoot =
                persistTrieNodes && header.number() > 0 && !m_incrementalMptLatchedOff;
            result =
                bcos::evm::engine::finalizeOpBlockResult(executor, view, header, execLedgerConfig,
                    cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr, incrementalRoot);

            // ⑤ Historical eth_call trie-node persistence (①a): keep the committed
            // stateRoot resolvable by HistoricalStateBackend at any later height.
            // buildAndCollect scans ONLY this block's delta layer (the view's top mutable
            // layer — preBlockOpSteps/serial-execute/finalizeBlock writes), reads parent nodes
            // through the view, commits both trie levels and flushes the new nodes itself; its
            // root replaces the skipped full rebuild. PRECONDITION: the parent header must be
            // COMMITTED (OP's single-slot m_pending never executes on a pending parent) and its
            // trie nodes persisted — i.e. scenario B must be on since genesis; a chain that
            // committed scenario-B blocks before node persistence existed (or activated the
            // flag mid-chain) fails LOUDLY here with MPTInvariantViolation rather than
            // rebuilding from an empty trie (BaselineScheduler's buildMPTStateRoot contract,
            // including its block-1 diagnostic, BaselineScheduler.h:505-519). Block 0 needs no
            // persistence here: OP never executes it — genesis nodes come from
            // Ledger::buildGenesisBlock's l2EthereumCompat import (Ledger.cpp:2391).
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
                    // Interim safety (root-divergence bug, 2026-08-23): cross-check the
                    // incremental root against the full rebuild over the same view. The two
                    // agree on every golden vector and the C3-shaped states, but DIVERGE on
                    // the C2-fresh op-deployer allocs (a per-execution empty "ghost" account
                    // enters the full walk but not the incremental delta scan, plus an
                    // iteration-order sensitivity — full triage in
                    // docs/2026-08-23-jovian-alignment-handoff.md). Until that is root-caused,
                    // the battle-tested FULL rebuild stays authoritative: on disagreement we
                    // adopt its root, latch ①a off for the process lifetime (later blocks
                    // skip ⑤ entirely and finalize does the full build), and this block's
                    // already-flushed node rows remain as harmless content-addressed orphans
                    // (nothing resolves the discarded incremental root).
                    bcos::evm::evmstate::Storage2State<ViewType> fullCheck(view);
                    auto const fullRoot =
                        bcos::evm::engine::detail::toBcosH256(bcos::evm::stateRootOf(fullCheck));
                    if (delta.stateRoot == fullRoot) [[likely]]
                    {
                        result.stateRoot = delta.stateRoot;
                    }
                    else
                    {
                        m_incrementalMptLatchedOff = true;
                        OP_SCHEDULER_LOG(WARNING)
                            << LOG_DESC(
                                   "①a incremental MPT root diverged from the full rebuild — "
                                   "falling back to the full root and latching ①a off for "
                                   "this process (historical eth_call/getProof serve "
                                   "latest-only until the divergence is fixed; see the "
                                   "Jovian alignment handoff doc)")
                            << LOG_KV("block", header.number())
                            << LOG_KV("incrementalRoot", delta.stateRoot.hexPrefixed())
                            << LOG_KV("fullRoot", fullRoot.hexPrefixed())
                            << LOG_KV("parentRoot", parentRoot.hexPrefixed());
                        result.stateRoot = fullRoot;
                    }
                }
                catch (const bcos::ledger::mpt::MPTInvariantViolation& e)
                {
                    // Named diagnosis (BaselineScheduler.h:505-519 contract): a missing node
                    // under the parent root means scenario-B node persistence was not active
                    // at the parent height — pre-①a chain, or feature_l2_ethereum_compat
                    // activated mid-chain. Halting here is intended; rebuilding over an empty
                    // trie would fork the stateRoot away from the announced header.
                    throw bcos::evm::engine::OpStorageError(fmt::format(
                        "OpScheduler: incremental MPT build at block {} failed — parent block "
                        "{}'s state root {} has no persisted trie nodes (scenario B / "
                        "feature_l2_ethereum_compat must be active since genesis): {}",
                        header.number(), header.number() - 1, parentRoot.hex(), e.what()));
                }
            }
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            // Keep the FISCO type and message; describeException/classifyException at the
            // skeleton backstop consume them (OpConsensusRejected / detailed reason).
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            // Keep the type and message — classifyException maps it to OpStorageFault (-32603).
            throw;
        }
        catch (const std::exception&)
        {
            throw;  // Bindable families → the skeleton classifies.
        }
        catch (...)
        {
            // Non-std::exception throw (raw evmone code paths can still produce these).
            // Normalize to a FISCO type so the skeleton's catch(std::exception&) binds and
            // classifyException receives a catchable one.
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: execute threw an unrecognized (non-std::exception) object; "
                "raw tx decode or block-level consensus fault");
        }

        // Announced block hash stashed for the commit hook (EthBlockHeader::computeHash on the
        // executed header would throw std::bad_optional_access — its optional fields are
        // incomplete).
        bcos::crypto::HashType announcedBlockHash =
            bcos::protocol::EthBlockHeader::computeHash(header);
        co_return ExecuteOutcome{std::move(result), announcedBlockHash};
    }

    /// ③ finish: write the OP commitments into the executedHeader (skip MPT; never call
    /// BlockHeader::hash() — the header is rebuilt from the announced payload).
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
        else if (blockHeader.blobGasUsed())
            // Pre-Jovian (Isthmus): the seal does not compute DA-footprint blobGasUsed,
            // but the announced OP header always carries blobGasUsed=0 (EIP-4844 leftover
            // in the 21-field RLP). Copy it so the six-way commitment check does not
            // spuriously reject on presence-asymmetry (nullopt vs 0).
            executedBlockHeader->setBlobGasUsed(*blockHeader.blobGasUsed());
        co_return executedBlockHeader;
    }

    /// ④ commit: persist the 7 SYS tables via ledger::prewriteBlockToBuffer into a standalone
    /// MutableStorage. blockHash = the execute phase's announcedBlockHash, never recomputed on the
    /// executed header (its optional fields are incomplete → would throw). writeNonces=false.
    /// **Registers the announced header (pending.block->blockHeader()), NOT the executed one** —
    /// the executed header's tars encode is incomplete (missing coinbase/gasLimit/baseFee/...),
    /// which a child block's parent-header read would reject.
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commitPersist(
        PendingBlock const& pending)
    {
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        // Guard: receipt count must equal tx count (opstackRegisterBlock invariant).
        if (pending.result.receipts.size() != pending.block->transactionsSize())
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a receipt count differing "
                                      "from the transaction count"});

        auto block = pending.block;
        // Idempotency: retrying a failed commit re-appends the same pending result; clear first.
        block->clearReceipts();
        for (auto const& r : pending.result.receipts)
            block->appendReceipt(r);
        // setStoreToBackend should only be set on the toShared() fresh copies; this reset is
        // defensive.
        for (auto const& tx : block->transactions())
            tx->setStoreToBackend(false);

        // blockTxs: same pattern as the getTransactions hook (toShared() is required for the
        // ConstPtr conversion).
        auto blockTxs = std::make_shared<protocol::ConstTransactions>(
            block->transactions() | ::ranges::views::transform([](auto tx) {
                return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
            }) |
            ::ranges::to<std::vector>());

        co_await bcos::ledger::prewriteBlockToBuffer(*m_ledger, blockTxs, block, *storage,
            pending.announcedBlockHash, /*writeNonces=*/false);
        co_return storage;
    }

    /// Commit continuity (head-advance guard): prewriteBlockToBuffer writes SYS_CURRENT_STATE
    /// unconditionally, so restore the monotonic guard (rejecting already-committed / discontinuous
    /// commits). Reads the storage view (where the OP commit writes), not the ledger's own
    /// m_stateStorage.
    task::Task<bool> commitContinuityCheck(protocol::BlockNumber number)
    {
        if (!isSysContractDeploy(number))
        {
            if (m_lastCommittedBlockNumber == -1)
            {
                auto view = m_multiLayerStorage->fork();
                m_lastCommittedBlockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
            }
            if (m_lastCommittedBlockNumber != -1 && number <= m_lastCommittedBlockNumber)
            {
                OP_SCHEDULER_LOG(INFO) << "Block already committed: " << number
                                       << "! latest: " << m_lastCommittedBlockNumber;
                co_return false;
            }
            if (m_lastCommittedBlockNumber != -1 && number - m_lastCommittedBlockNumber != 1)
            {
                OP_SCHEDULER_LOG(INFO) << "Discontinuous commit block number: " << number
                                       << "! expect: " << (m_lastCommittedBlockNumber + 1);
                co_return false;
            }
        }
        co_return true;
    }

    /// OP notifiers are no-ops registered in the ctor (no txpool / RPC push on the OP path).
    void notifyBlockNumber(protocol::BlockNumber number)
    {
        m_blockNumberNotifier(number);
        m_transactionNotifier(number, std::make_shared<bcos::protocol::TransactionSubmitResults>(),
            [](const Error::Ptr&) {});
    }

    /// The skeleton default calls header.hash(), which throws for an OP header — build the
    /// LedgerConfig by hand (features only).
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

    /// Same: the skeleton default calls header.hash(); the OP commit path never does.
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

public:
    /// Round-2 F4: the RPC-bound reason string per classified code. Both catch arms of call() /
    /// callAtBlock must produce the SAME string for the same code — which arm fires is
    /// unreliable in wedprcrypto-linked binaries (observed: an OpStorageError thrown after
    /// the executor co_await escapes catch(std::exception&) yet classifyException still types
    /// it; mechanism: the Rust libs' bundled runtime breaks libc++ base-class exception
    /// matching binary-wide — precedent bcos-rpc/test/CMakeLists.txt:29-36, NOT -fno-rtti),
    /// so the classification carries the semantics and this carries the wording.
    static constexpr std::string_view rpcSafeReason(scheduler::SchedulerError code) noexcept
    {
        return code == scheduler::SchedulerError::OpStorageFault      ? "storage fault" :
               code == scheduler::SchedulerError::OpConsensusRejected ? "consensus rejection" :
                                                                        "internal error";
    }

    // Exception classification: OpConsensusError→OpConsensusRejected / OpStorageError→
    // OpStorageFault / MPT read-path faults (MPTInvariantViolation: missing node,
    // MPTDecodeError: corrupt node — both mean the persisted trie is unreadable)→
    // OpStorageFault / other→UnknownError. The mpt tier matters because Storage2State's
    // poison ladder only catches its own read wrapper: a missing/corrupt node can escape
    // as a raw mpt exception (e.g. from loadOpFeeParams' first trie walk), and without
    // this tier it would surface as UnknownError instead of the storage-fault semantics.
    // Public: unit-tested directly (ClassifyExceptionMapping).
    scheduler::SchedulerError classifyException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError&)
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

    /// Error-message recovery at the catch(...) backstop: rethrow + typed catch, so the engine
    /// barrier can emit a detailed validationError. Prefer what() from the OP error families;
    /// history note — the former catch-all "RTTI typed-catch bypassed" outcome was not an
    /// evmone-RTTI property at all: the pre-fix evmone port (compiled -fno-rtti) injected a
    /// private typeinfo(std::exception) copy into the link, so EVERY catch(std::exception&)
    /// in the binary compared against a different typeinfo address than the runtime used and
    /// silently fell to catch(...). The port now builds evmone with RTTI (ports/evmone,
    /// rtti-enabled.patch); a plain catch(const std::exception&) here would bind fine — the
    /// OP-specific catches stay first so their exact messages win.
    std::string describeException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError& e)
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
    /// Project an executed/announced header's commitment fields into the six-way comparison
    /// surface; both sides read the same accessors.
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

    /// OP eth_call: fork the latest committed state, build a real OP block context (hand-built
    /// LedgerConfig), load the L1Block fee params, run OpstackExecutor::executeTransaction
    /// (injecting chainId/blockGasLeft/block hashes), then discard the fork (dry-run).
    /// Shared call-execution tail of coCallLatest / coCallAtBlock (round-2 F1 dedup — the two
    /// used to carry ~35 near-identical lines each, and the round-1 poison/sharedError fix had
    /// to be written twice): load fee params with a poison check → blockGasLeft → block-hash
    /// window → per-call executor → executeTransaction → loud sharedError/hashErr checks.
    /// @p errTag is only the message-prefix discriminator ("call" vs "historical") — a
    /// string_view; both call sites pass string literals, so it outlives the coroutine.
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
        // Storage2State swallows storage faults into the poison flag (its noexcept read
        // contract) — without this check a corrupted read would degrade to zero fee params /
        // absent accounts and the call would return a plausible-looking WRONG answer.
        if (stateView.poisoned())
            throw bcos::evm::engine::OpStorageError(fmt::format(
                "OpScheduler: {} fee-param read fault: {}", errTag, stateView.firstError()));
        const auto blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "OpScheduler blockGasLeft"));

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<AnyView> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // Construct the executor per call (one evmc::VM).
        auto sharedError = std::make_shared<SharedErrorSlot>();
        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

        auto receipt = co_await executor.executeTransaction(view, header, transaction,
            /*contextID=*/0, ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        // The executor's internal Storage2State instances report read faults to sharedError
        // (same poison contract) — fail the call loudly instead of returning a receipt built
        // on swallowed zero-value reads.
        // Stage-attribution invariant (round-3 S5): fee-param-stage read faults trip the poison
        // check above; EXECUTION-stage faults (a missing/corrupt node hit by an intra-call SLOAD
        // on the historical trie — CallAtBlockExecutionStageNodeMissingIsStorageFault) can only
        // surface HERE, via sharedError. Both tiers must stay loud.
        std::string firstError;
        {
            // SharedErrorSlot access contract (Storage2State.h): every read takes the mutex.
            std::lock_guard lock(sharedError->mutex);
            firstError = sharedError->message;
        }
        if (!firstError.empty())
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} state read fault: {}", errTag, firstError));
        if (hashErr.has_value())
            // Block-hash lookup reads storage — a fault is OpStorageError (→ OpStorageFault via
            // classifyException), matching the block path (OpBlockExecute.h finalizeOpBlockResult),
            // not a bare runtime_error mapped to UnknownError (round-3 S1).
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} block-hash lookup failed: {}", errTag, *hashErr));
        co_return receipt;
    }

    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;

        auto view = m_multiLayerStorage->fork();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, *m_blockFactory);
        // Keep the header Ptr alive: blockHeader() returns a fresh shared_ptr BY VALUE; binding a
        // reference to it would dangle a temporary.
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

    /// OP historical eth_call (③ + ①b): execute @p transaction against the state block
    /// @p blockNumber committed — the MPT at that block's header stateRoot — keeping the full
    /// OP execution semantics of coCallLatest (same OpstackExecutor, hand-built LedgerConfig,
    /// historical L1 fee params and block-hash window). The call's own writes land in a fresh
    /// mutable layer over a HistoricalStateBackend (read-your-writes, nothing persisted).
    ///
    /// Boundary sequence mirrors BaselineScheduler::callAtBlock (same codes, same boundary
    /// order; the feature-gate message drops Baseline's trailing "not completely committed"
    /// clause): beyond-latest → InvalidBlockNumber; block == latest → the coCallLatest fast
    /// path; non-scenario-B chain (no feature_l2_ethereum_compat) → InvalidStatus; a missing or
    /// unpersisted state root → InvalidStatus (loud refusal, never a latest-state answer
    /// dressed up as a historical one). Returns (Error, receipt) — the Error half carries
    /// the refusal so callAtBlock's catch wrappers only see real exceptions.
    task::Task<std::tuple<Error::Ptr, protocol::TransactionReceipt::Ptr>> coCallAtBlock(
        protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber)
    {
        namespace op = bcos::evm::opstack;

        auto latestView = m_multiLayerStorage->fork();
        auto latestNumber =
            co_await bcos::ledger::getCurrentBlockNumber(latestView, bcos::ledger::fromStorage);
        // blockNumber < 0: refuse semantically (BaselineScheduler shares this hole — a negative
        // number falls through to the storage read and fails unclassified); getBlockData on a
        // negative height is not a meaningful lookup either way.
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
            // Fast path parity note (pre-existing BaselineScheduler deviation, documented not
            // fixed): latestNumber is read once up front, so a commit landing between this read
            // and coCallLatest's own fork makes "latest" here one height stale (benign TOCTOU —
            // the call still executes on a committed state, just the previous one), and the
            // latest view is forked twice (here + inside coCallLatest). Not worth diverging from
            // the Baseline boundary order to fix.
            co_return std::tuple{
                Error::Ptr{nullptr}, co_await coCallLatest(std::move(transaction))};
        }

        // OQ6 gate: only scenario B (feature_l2_ethereum_compat) commits the COMPLETE state to
        // the trie; anywhere else a historical call could silently read dormant accounts as
        // absent. Features are read at the pinned height from the latest view (SYS_CONFIG is
        // pass-through metadata, not historicised state).
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
        // Keep the header Ptr alive: blockHeader() returns a fresh shared_ptr BY VALUE; binding a
        // reference to it would dangle a temporary (same guard as coCallLatest).
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
        // Loud refusal when the root's trie nodes were never persisted (a chain that ran
        // before ①b node persistence). The empty root needs no rows — it IS the empty trie.
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

        // The historical stack: a fresh writable layer over the read-through backend, so the
        // call's own writes read back inside the call and die with the view (dry-run).
        using HistoricalBackend = bcos::scheduler_v1::HistoricalStateBackend<ViewType>;
        HistoricalBackend historicalBackend(latestView, stateRoot);
        storage2::View<typename MultiLayerStorage::MutableStorage, void, HistoricalBackend>
            historicalView(std::addressof(historicalBackend));
        historicalView.newMutable();
        // The shared tail (coCallOnView) then loads the historical L1Block fee params through
        // this view (poison-checked), builds the block-hash window, and runs the executor —
        // a historical read that hits a missing trie node mid-execution surfaces via its
        // sharedError check, not as a status-ok receipt built on swallowed zero values.
        co_return std::tuple{Error::Ptr{nullptr}, co_await coCallOnView(historicalView, header,
                                                      *transaction, *ledgerConfig, "historical")};
    }

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkFlags m_forkFlags;

    // Orchestration state (was the skeleton's protected block).
    MultiLayerStorage* m_multiLayerStorage = nullptr;
    bcos::protocol::BlockFactory* m_blockFactory = nullptr;
    bcos::ledger::LedgerInterface* m_ledger = nullptr;
    bcos::IOServicePool::Ptr m_ioServicePool;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(bcos::Error::Ptr)>)>
        m_transactionNotifier;
    std::mutex m_executeMutex;
    int64_t m_lastExecutedBlockNumber{-1};
    std::mutex m_commitMutex;
    int64_t m_lastCommittedBlockNumber{-1};
    /// ①a kill-latch (root-divergence interim, see execute() ⑤): tripped once the incremental
    /// MPT root disagrees with the full rebuild — all later blocks build the root the
    /// battle-tested full-rebuild way and skip node persistence. Execute is single-flight
    /// under m_executeMutex; atomic only for defensive simplicity.
    std::atomic_bool m_incrementalMptLatchedOff{false};
    std::mutex m_pendingMutex;
    std::optional<PendingBlock> m_pending;
};


}  // namespace bcos::executor_v1::opstack
