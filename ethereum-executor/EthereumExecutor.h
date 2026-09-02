/// @file EthereumExecutor.h
/// @brief A pure-Ethereum transaction executor.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept. The outer EVM
/// logic (state machine, host, transaction validation / execution / block
/// finalization) is ported from bcos-evm and integrated here so that it reads
/// and writes BCOS storage directly through ledger::account::EVMAccount +
/// storage2 — there is no evmone::state::StateView / BlockHashes / StateDiff
/// adapter layer (StorageStateView / StorageBlockHashes / applyStateDiff are
/// gone), transactions are the bcos protocol::Transaction directly, and block
/// headers are the bcos protocol::BlockHeader directly (see the
/// Rollbackable / EthereumState / EthereumHost / EthereumTransition headers).
///
/// This executor does NOT support FISCO BCOS native transaction features
/// (precompiled management, auth committee, gas payment precheck, etc.).
/// It is designed for EEST (Ethereum Execution Spec Tests) compliance
/// testing and Ethereum-compatible chain execution.
///
/// Strict nonce ordering is a precondition for use: Ethereum semantics require
/// each transaction's nonce to equal the sender's current storage nonce, so a
/// sender's transactions must be presented in nonce order. If a block contains
/// a sender's transactions out of order, the higher-nonce one validates as
/// NONCE_TOO_HIGH and is dropped as a failure receipt (it must be resubmitted).
/// Whoever wires this executor in must therefore ensure the sealer packs each
/// sender's transactions in nonce order.

#pragma once

#include "EthereumTransition.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/RollbackableStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Exceptions.h>
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace bcos::executor_v1::eth
{
// The journaling storage wrapper is shared with the transaction-executor (now
// defined in bcos-framework/storage2/RollbackableStorage.h).
using bcos::executor_v1::Rollbackable;

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);

// Non-template helpers — defined in EthereumExecutor.cpp.
/// Build the EVM block parameters from the BCOS BlockHeader + LedgerConfig.
EthBlockInfo buildBlockInfo(
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config, evmc_revision rev);

/// Builds a failed BCOS receipt for a transaction that failed validation
/// (see evm::ErrorCode in EVMSupport.h).
protocol::TransactionReceipt::Ptr validationErrorReceipt(std::error_code const& error,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber);

class EthereumExecutor
{
public:
    /// @param blockHashLookup Optional BLOCKHASH provider used for BLOCKHASH
    ///                        lookups during execution. It is called with the
    ///                        queried height and the executing block's height
    ///                        (from the execution context, so a storage-backed
    ///                        provider need not read the current height itself).
    ///                        When omitted, all BLOCKHASH reads return zero
    ///                        (Ethereum "unknown block" semantics). In production
    ///                        a storage-backed provider resolving
    ///                        ledger::getBlockHash is injected (see libinitializer);
    ///                        EEST injects an in-memory map.
    ///
    /// The injected provider is deliberately bound to the storage handed to
    /// the *constructor* (the committed/global storage), not to the per-chunk
    /// view a transaction executes against: BLOCKHASH resolves historical
    /// committed hashes, which are never modified by the block being executed.
    EthereumExecutor(protocol::TransactionReceiptFactory const& receiptFactory,
        BlockHashLookup blockHashLookup = {})
      : m_receiptFactory(receiptFactory),
        m_vm(evmc_create_evmone()),
        m_blockHashLookup(std::move(blockHashLookup))
    {}

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// Execute a single Ethereum transaction.
    ///
    /// Convenience entry point that drives the three lifecycle phases in
    /// order: prepare() → execute() → finish(). See ExecuteContext for the
    /// per-phase semantics.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executeContext.prepare();
        co_await executeContext.execute();
        co_return co_await executeContext.finish();
    }

    /// Block-level finalization: apply block rewards and withdrawals.
    /// Call this after all transactions in a block have been executed.
    template <class Storage>
    task::Task<void> finalizeBlock(Storage& storage, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig, evmc_revision rev,
        std::optional<uint64_t> blockReward, std::vector<EthWithdrawal> const& withdrawals = {})
    {
        // Same all-or-nothing guarantee as execute(): if finalizeState throws
        // part-way (a reward/withdrawal storage write fails), roll the journal
        // back so the block's rewards are not left half-applied, then rethrow.
        Rollbackable<Storage> rollable(storage);
        EthereumState<Rollbackable<Storage>> state(rollable);
        const auto savepoint = rollable.current();
        // co_await is not permitted inside an exception handler ([expr.await]/2),
        // so the failure is captured here and the rollback runs after it.
        std::exception_ptr failure;

        try
        {
            evmc_address coinbase{};
            auto const& coinbaseBytes = blockHeader.coinbase();
            if (coinbaseBytes.size() == sizeof(evmc_address))
            {
                std::copy_n(coinbaseBytes.begin(), sizeof(evmc_address), coinbase.bytes);
            }

            co_await finalizeState(state, rev, coinbase, blockReward, withdrawals);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (failure)
        {
            try
            {
                co_await rollable.rollback(savepoint);
            }
            catch (...)
            {
                // Ignore the cleanup error; the original failure wins.
            }
            std::rethrow_exception(failure);
        }
    }

    // TransactionExecutor concept support: ExecuteContext
    //
    // The scheduler drives a multi-stage pipeline: for each transaction it
    // creates an ExecuteContext (createExecuteContext) and then runs
    // prepare(), execute() and finish() — potentially for many transactions
    // concurrently (SchedulerParallelImpl) or with overlapping pipeline
    // stages (SchedulerSerialImpl).
    //
    // Thread-safety: every mutable execution state is owned by the individual
    // context (never shared between contexts), so the three phases of
    // different transactions may run in parallel. The only state shared
    // through the executor is:
    //   - m_vm              : evmone VM execution is thread-safe (a fresh
    //                         execution state is created per call).
    //   - m_receiptFactory  : createReceipt allocates a fresh receipt per call.
    //   - m_blockHashLookup : invoked for BLOCKHASH. The production provider
    //                         reads a read-only committed table straight
    //                         through the injected storage (see libinitializer)
    //                         and therefore relies on that Storage's support
    //                         for concurrent reads; an absent provider is
    //                         stateless.
    template <class Storage>
    struct ExecuteContext
    {
        std::reference_wrapper<EthereumExecutor> executor;
        std::reference_wrapper<Storage> storage;
        std::reference_wrapper<protocol::BlockHeader const> blockHeader;
        std::reference_wrapper<protocol::Transaction const> transaction;
        int contextID;
        std::reference_wrapper<ledger::LedgerConfig const> ledgerConfig;
        // Reserved for eth_call (dry-run, no state persistence). The scheduler
        // always drives real execution with call=false; the field is part of
        // the TransactionExecutor concept signature.
        bool call;

        // Per-phase state — owned by this context, safe for concurrent phases.
        evmc_revision m_rev = EVMC_FRONTIER;
        EthBlockInfo m_blockInfo;
        EthCallParams m_callParams;
        int64_t m_blobGasLeft = 0;
        // Set when execute()'s validate_transaction failed against the state
        // the transaction would actually run on. Validation runs once, in
        // execute(), against the current state (after earlier same-chunk
        // transactions have had their writes applied); a failure here means the
        // transaction is genuinely invalid and gets a failure receipt from
        // finish(), with no state written for it.
        std::optional<std::error_code> m_validationError;
        protocol::TransactionReceipt::Ptr m_receipt;

        ExecuteContext(EthereumExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(std::ref(exec)),
            storage(std::ref(st)),
            blockHeader(std::ref(bh)),
            transaction(std::ref(tx)),
            contextID(cid),
            ledgerConfig(std::ref(cfg)),
            call(c)
        {}

        /// Node chain id for EIP-7702 auth validation; 0 if unconfigured.
        uint64_t nodeChainId() const
        {
            if (auto const& cid = ledgerConfig.get().chainId(); cid.has_value())
                // fold of the trailing 8 bytes == the low 64 bits of the BE value, the
                // same truncation the old static_cast<uint64_t>(intx-load) produced
                return bcos::fromBigEndian<uint64_t>(cid->bytes);
            return 0;
        }

        /// Phase 1 — setup (no state access).
        ///
        /// Resolves the block-level parameters (revision, block info, blob
        /// schedule) and the eth_call normalization overrides. The scheduler
        /// batches prepare() across a whole chunk (and the serial pipeline
        /// overlaps a chunk's prepare filter with the previous chunk's execute
        /// filter), so this phase deliberately reads no state: validation
        /// happens once, in execute(), against the state the transaction
        /// actually executes on.
        ///
        /// blob_gas_left is the block's remaining blob gas (max_blob_gas_per_block
        /// minus already-included blobs). It must NOT be the tx's own blob gas,
        /// otherwise a tx with too many blobs would pass validation.
        /// EIP-7840 blob schedule constants live in EthereumTransition.h so
        /// both this executor and buildBlockInfo share them.
        task::Task<void> prepare()
        {
            auto revOpt = ledgerConfig.get().evmcRevisionForBlock(blockHeader.get().number());
            if (!revOpt.has_value())
            {
                BOOST_THROW_EXCEPTION(
                    EvmcRevisionNotConfigured() << errinfo_comment("evmcRevision not configured"));
            }
            m_rev = *revOpt;
            m_blockInfo = buildBlockInfo(blockHeader.get(), ledgerConfig.get(), m_rev);
            m_callParams = {};
            m_validationError.reset();
            m_receipt.reset();
            if (call)
            {
                // Dry-run (eth_call / eth_estimateGas): normalize to Ethereum
                // RPC semantics so the standard MetaMask / ethers.js call shape
                // — no gas, no nonce, no gasPrice — is not rejected by
                // consensus-level admission checks:
                //   * omitted gas -> the block gas limit, capped to the per-tx
                //     limit on Osaka+ (EIP-7825: MAX_TX_GAS_LIMIT = 2^24).
                //   * price: a dry run is never charged, so clear gas price,
                //     tip and the block base fee unconditionally (geth's
                //     NoBaseFee for eth_call).
                if (transaction.get().gasLimit() == 0)
                {
                    auto gasCap = m_blockInfo.gas_limit;
                    if (m_rev >= EVMC_OSAKA)
                    {
                        gasCap = std::min<int64_t>(gasCap, evm::MAX_TX_GAS_LIMIT);
                    }
                    m_callParams.gasLimit = gasCap;
                }
                m_callParams.free = true;
                m_blockInfo.base_fee = 0;
                // blob_base_fee is intentionally NOT zeroed for the dry run:
                // geth's eth_call still enforces the blob-fee cap
                // (BLOB_FEE_CAP_LESS_THAN_BLOCKS) and still includes the blob
                // fee in the balance check, so a blob eth_call / estimateGas
                // keeps those semantics. Only the non-blob price is cleared.
                // * omitted nonce -> the sender's current nonce. This needs a
                //   state read, so it is resolved in execute() (the only
                //   phase allowed to touch state).
            }
            m_blobGasLeft =
                static_cast<int64_t>(evm::max_blob_gas_per_block(blobParamsForRevision(m_rev)));
            co_return;
        }

        /// Phase 2 — validate & execute.
        ///
        /// Runs validateTransaction() against the current storage state, then —
        /// if valid — runTransaction() and immediately applies the resulting
        /// state changes back to the storage.
        ///
        /// Validation happens here, once per transaction, against the state the
        /// transaction actually executes on. It must not live in prepare():
        /// the schedulers batch prepare() for a whole chunk before any
        /// execute(), so validating there would run every transaction against
        /// the pre-chunk state. A transaction that fails this validation is
        /// genuinely invalid: it is skipped (no state written) and finish()
        /// emits a failure receipt.
        ///
        /// The state is applied here — not in finish() — so transactions within
        /// a chunk have sequential read-through semantics, matching
        /// TransactionExecutorImpl: both schedulers batch each phase across a
        /// whole chunk, so if the writes were only applied in finish(), every
        /// transaction in the chunk would execute against the same unmodified
        /// state.
        task::Task<void> execute()
        {
            // The per-transaction state is local to this phase: it reads the
            // current storage (earlier same-chunk transactions' writes already
            // landed) through a journaling Rollbackable wrapper.
            //
            // NOTE (dry-run): runTransaction() unconditionally writes the state
            // back via applyToStorage(), including for call=true. A dry-run
            // (eth_call / eth_estimateGas) therefore leaves no trace ONLY
            // because every call=true call site executes against a throwaway
            // forked view (coCallLatest / callAtBlock); do not add a call=true
            // path that runs against the live storage.
            //
            // Transaction atomicity (all-or-nothing): take a savepoint up front
            // so that if anything below throws — e.g. a storage write inside
            // runTransaction()->applyToStorage() fails part-way — the journal
            // can undo every write this transaction applied to the underlying
            // storage, instead of leaving partial writes behind. The receipt is
            // only produced in finish(), so no receipt leaks for a rolled-back
            // transaction; the exception is rethrown for the caller to handle.
            // Per-transaction journaling costs one in-memory pre-image read +
            // Record copy per writeOne (a value transfer ≈ 6-9 writes). This
            // is the deliberate price of all-or-nothing atomicity on a partial
            // applyToStorage failure — a correctness improvement over the old
            // executor, which could leave partial writes behind. On the happy
            // path the journal is discarded with this local scope (no
            // cross-block accumulation).
            Rollbackable<Storage> rollable(storage.get());
            EthereumState<Rollbackable<Storage>> state(rollable);
            const auto savepoint = rollable.current();
            // co_await is not permitted inside an exception handler ([expr.await]/2),
            // so the failure is captured here and the rollback runs after it.
            std::exception_ptr failure;

            try
            {
                if (call && transaction.get().nonce().empty())
                {
                    // Resolve the sender's current nonce for the eth_call shape
                    // (0 would fail NONCE_TOO_LOW for a sender past its first tx).
                    auto senderAcc = state.find(ethSender(transaction.get()));
                    m_callParams.nonce = senderAcc ? senderAcc->nonce : 0;
                }

                auto validationResult = validateTransaction(state, m_blockInfo, transaction.get(),
                    m_rev, m_blockInfo.gas_limit /*block_gas_left*/,
                    m_blobGasLeft /*blob_gas_left*/, m_callParams);
                if (auto* props = std::get_if<EthTxProperties>(&validationResult))
                {
                    // Valid against the current state — use the computed properties.
                    // NOTE: we do not special-case SENDER_NOT_EOA here. EIP-3607
                    // requires rejecting transactions whose sender has non-delegating
                    // code; only accounts with empty code or a 0xef0100 delegation
                    // designator may be senders, which validate_transaction already
                    // accepts.
                    m_receipt = co_await runTransaction(state, m_blockInfo,
                        executor.get().m_blockHashLookup, transaction.get(), m_rev,
                        executor.get().m_vm, *props, nodeChainId(), m_callParams,
                        executor.get().m_receiptFactory, blockHeader.get().number());
                }
                else
                {
                    // Genuinely invalid — skip execution; finish() produces a
                    // failure receipt and nothing is written for this transaction.
                    m_validationError = std::get<std::error_code>(validationResult);
                    BCOS_LOG(INFO) << LOG_BADGE("EXECUTE") << LOG_DESC("tx validation failed")
                                   << LOG_KV("error", m_validationError.value().message())
                                   << LOG_KV("code", m_validationError.value().value());
                }
            }
            catch (...)
            {
                // Record the failure (any writes already applied to the
                // underlying storage are undone below).
                failure = std::current_exception();
            }
            if (failure)
            {
                // Undo every write this transaction applied to the underlying
                // storage, restoring the pre-transaction state, then rethrow.
                // If the rollback itself fails, keep the original failure — it
                // is the real cause worth reporting (a failed rollback leaves
                // partial writes, which is no worse than today).
                try
                {
                    co_await rollable.rollback(savepoint);
                }
                catch (...)
                {
                    // Ignore the cleanup error; the original failure wins.
                }
                std::rethrow_exception(failure);
            }
            co_return;
        }

        /// Phase 3 — finalize.
        ///
        /// Produces the BCOS receipt. The state was already applied in
        /// execute(); this phase only returns the receipt built there. The one
        /// exception is a transaction that failed validation in execute(): it
        /// gets a failure receipt and no state was written for it.
        ///
        /// Known limitation: the produced receipt carries no contract return
        /// data. The evmc host does not retain output (return data is consumed
        /// during execution), so under executor_version=2 an eth_call on a
        /// contract read returns empty data — the call executes
        /// (status/gasUsed are correct) but the return value is not surfaced.
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            if (m_validationError.has_value())
            {
                co_return validationErrorReceipt(*m_validationError,
                    executor.get().m_receiptFactory, blockHeader.get().number());
            }
            co_return m_receipt;
        }
    };

    template <class Storage>
    task::Task<ExecuteContext<std::decay_t<Storage>>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<std::decay_t<Storage>>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

private:
    protocol::TransactionReceiptFactory const& m_receiptFactory;
    evmc::VM m_vm;
    BlockHashLookup m_blockHashLookup;
};

}  // namespace bcos::executor_v1::eth
