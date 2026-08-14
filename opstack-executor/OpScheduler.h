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
#include <bcos-ledger/LedgerMethods.h>  // getCurrentBlockNumber / getBlockData CPO tag_invoke
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>  // serial per-tx scheduler (Task 4)
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

    void reset(std::function<void(Error::Ptr)> callback) override { callback(nullptr); }

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
            catch (const std::exception& e)
            {
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
            catch (...)
            {
                // Typed-catch RTTI bypass: catch(std::exception&) does not reliably bind across the
                // -fno-rtti evmone boundary — return an Error rather than dangling the coroutine.
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "OpScheduler::call: unknown (RTTI-bypassed) exception"),
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

            auto outcome = co_await execute(view, *blockHeader, transactions, *ledgerConfig);

            // finish: write the OP commitments into the executedHeader (skip MPT; never call
            // BlockHeader::hash() — the header is rebuilt from the announced payload).
            bool sysBlock = false;
            auto executedHeader = co_await finishExecute(
                view, outcome, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // verify: unconditional six-way comparison; a mismatch throws OpConsensusError →
            // OpConsensusRejected.
            namespace engine = bcos::evm::engine;
            if (auto mismatch = engine::mismatchedFieldOf(
                    headerCommitments(*executedHeader), headerCommitments(*blockHeader)))
            {
                throw engine::OpConsensusError(
                    "OpScheduler: six-way commitment mismatch on field " + *mismatch);
            }

            // Push the execute view onto the MLS layer stack (mergeBackStorage in the commit
            // phase requires a non-empty stack — the pushed view IS the block's state delta).
            m_multiLayerStorage->pushView(std::move(view));

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
            // RTTI-bypass fallback: -fno-rtti's libevmone.a carries non-unique typeinfo, so
            // runtime_error subclasses (OpConsensusError/OpStorageError) escape the
            // catch(std::exception&) above. Without this, OP exceptions bypass classifyException
            // and leak out of executeBlock. classifyException's rethrow + typed catch binds
            // reliably on the FISCO types normalized by the execute hook's own catch(...).
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
    task::Task<ExecuteOutcome> execute(ViewType& view, protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig)
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
                else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 &&
                         typeByte != 0x04)
                    throw bcos::evm::engine::OpConsensusError(
                        "OpScheduler: unsupported tx type byte 0x" + std::to_string(typeByte));
            }

            bcos::ledger::LedgerConfig execLedgerConfig;
            execLedgerConfig.setEVMCRevision(cfg.rev);

            auto sharedError = std::make_shared<std::string>();
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
                m_hashImpl, hashes, hashErr, daFootprintGasScalar);

            // ② Per-block context (fee NOT loaded here — lazily on the first NORMAL tx's prepare).
            // blockGasLeft via narrowU256ToU64 (silent-truncation guard, same as coCallLatest).
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
            result = bcos::evm::engine::finalizeOpBlockResult(executor, view, header,
                execLedgerConfig, cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr);
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            // FISCO types bind reliably — rethrow as-is, preserving the message (describeException
            // restores it at the skeleton backstop). Do not fall to catch(std::exception&) (the
            // -fno-rtti evmone boundary's typeinfo is unreliable) or catch(...) (message lost).
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
            // RTTI bypass: -fno-rtti evmone types escape the typed catch with broken typeinfo and
            // would bypass the skeleton's catch(std::exception&). Normalize to a FISCO type so
            // classifyException receives a catchable one.
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: execute threw an unrecognized (RTTI-bypassed) exception; "
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
    // Exception classification: OpConsensusError→OpConsensusRejected / OpStorageError→
    // OpStorageFault / other→UnknownError. Public: unit-tested directly (three-way mapping).
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
        catch (...)
        {
            return scheduler::SchedulerError::UnknownError;
        }
    }

    /// Error-message recovery at the catch(...) backstop: rethrow + typed catch, so the engine
    /// barrier can emit a detailed validationError. FISCO types bind reliably and yield what();
    /// catch(std::exception&) cannot reliably bind across the -fno-rtti evmone boundary.
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
        catch (...)
        {
            return "unclassified exception, RTTI typed-catch bypassed";
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
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

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

        bcos::evm::evmstate::Storage2State<ViewType> stateView(view);
        auto fee = op::loadOpFeeParams(stateView);
        const auto blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "OpScheduler blockGasLeft"));

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<ViewType> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // Construct the executor per call (one evmc::VM).
        auto sharedError = std::make_shared<std::string>();
        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
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
    std::mutex m_pendingMutex;
    std::optional<PendingBlock> m_pending;
};


}  // namespace bcos::executor_v1::opstack
