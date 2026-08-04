/// @file EthereumExecutor.h
/// @brief A pure-Ethereum transaction executor based on bcos-evm.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept.
/// Internally delegates all EVM execution and transaction validation to
/// the bcos-evm library (evmone::state::transition / validate_transaction),
/// which provides full Ethereum consensus compatibility.
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

#include "BCOS2Evmone.h"
#include "StorageStateView.h"
#include "bcos-evm/eth/state/errors.hpp"
#include "bcos-evm/eth/state/state.hpp"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include <bcos-utilities/Exceptions.h>
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace bcos::executor_v1::eth
{

DERIVE_BCOS_EXCEPTION(NonceAtMaxValue);
DERIVE_BCOS_EXCEPTION(GasPriceBelowBaseFee);
DERIVE_BCOS_EXCEPTION(PriorityGasPriceExceedsMax);
DERIVE_BCOS_EXCEPTION(IntrinsicGasTooLow);
DERIVE_BCOS_EXCEPTION(BlobTxMissingTo);
DERIVE_BCOS_EXCEPTION(EmptyBlobVersionedHashes);
DERIVE_BCOS_EXCEPTION(BlobFeeCapBelowBaseFee);
DERIVE_BCOS_EXCEPTION(InsufficientBalance);
DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(TransactionValidationFailed);

class EthereumExecutor
{
public:
    /// @param blockHashes Optional BlockHashes provider used for BLOCKHASH
    ///                     lookups during execution. When omitted, all BLOCKHASH
    ///                     reads return zero (Ethereum "unknown block"
    ///                     semantics). A storage-backed provider that reads
    ///                     hashes through the ledger::getBlockHash LedgerMethod
    ///                     is provided by StorageBlockHashes.h.
    ///
    /// The injected provider is deliberately bound to the storage handed to
    /// the *constructor* (the committed/global storage), not to the per-chunk
    /// view a transaction executes against: BLOCKHASH resolves historical
    /// committed hashes, which are never modified by the block being executed.
    /// This means the executor can be reused across blocks as long as the
    /// committed storage backing the provider outlives it.
    EthereumExecutor(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, std::shared_ptr<evmone::state::BlockHashes> blockHashes = {})
      : m_receiptFactory(receiptFactory),
        m_hashImpl(std::move(hashImpl)),
        m_vm(evmc_create_evmone()),
        m_blockHashes(std::move(blockHashes))
    {}

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// The BlockHashes provider used for BLOCKHASH lookups during execution.
    /// Falls back to all-zero hashes when none was injected at construction.
    evmone::state::BlockHashes const& blockHashes() const noexcept
    {
        return m_blockHashes ? *m_blockHashes :
                               static_cast<evmone::state::BlockHashes const&>(m_zeroBlockHashes);
    }

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
        std::optional<uint64_t> blockReward,
        std::vector<evmone::state::Withdrawal> const& withdrawals = {})
    {
        StorageStateView<Storage> stateView(storage);

        evmc_address coinbase{};
        auto const& coinbaseBytes = blockHeader.coinbase();
        if (coinbaseBytes.size() == sizeof(evmc_address))
        {
            std::copy_n(coinbaseBytes.begin(), sizeof(evmc_address), coinbase.bytes);
        }

        auto diff = evmone::state::finalize(stateView, rev, coinbase, blockReward, {}, withdrawals);

        co_await applyStateDiff(storage, diff, rev, *m_hashImpl);
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
    //   - m_vm            : evmone VM execution is thread-safe (a fresh
    //                       execution state is created per call).
    //   - m_hashImpl      : stateless hash computation, safe for concurrency.
    //   - m_receiptFactory: createReceipt allocates a fresh receipt per call.
    //   - m_blockHashes   : const noexcept lookups. StorageBlockHashes reads
    //                       straight through to a read-only committed table
    //                       and therefore relies on the injected Storage's
    //                       support for concurrent reads (see StorageBlockHashes.h);
    //                       ZeroBlockHashes is stateless.
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
        evmone::state::BlockInfo m_blockInfo;
        evmone::state::Transaction m_evmTx;
        StorageStateView<Storage> m_stateView;
        evmone::state::TransactionProperties m_txProps;
        evmone::state::BlobParams m_blobParams;
        int64_t m_blobGasLeft = 0;
        // Set when execute()'s validate_transaction failed against the state
        // the transaction would actually run on. Validation runs once, in
        // execute(), against the current state (after earlier same-chunk
        // transactions have had their writes applied); a failure here means the
        // transaction is genuinely invalid and gets a failure receipt from
        // finish(), with no state written for it.
        std::optional<std::error_code> m_validationError;
        evmone::state::TransactionReceipt m_evmReceipt;

        ExecuteContext(EthereumExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(std::ref(exec)),
            storage(std::ref(st)),
            blockHeader(std::ref(bh)),
            transaction(std::ref(tx)),
            contextID(cid),
            ledgerConfig(std::ref(cfg)),
            call(c),
            m_stateView(storage.get())
        {}

        /// Phase 1 — setup (no state access).
        ///
        /// Converts the BCOS types to evmone types and resolves the block-level
        /// parameters (revision, block info, blob schedule). The scheduler
        /// batches prepare() across a whole chunk (and the serial pipeline
        /// overlaps a chunk's prepare filter with the previous chunk's execute
        /// filter), so this phase deliberately reads no state: validation
        /// happens once, in execute(), against the state the transaction
        /// actually executes on.
        ///
        /// blob_gas_left is the block's remaining blob gas (max_blob_gas_per_block
        /// minus already-included blobs). It must NOT be the tx's own blob gas,
        /// otherwise a tx with too many blobs would pass validation. Matches
        /// evmone's t8n/blockchaintest runners which start blob_gas_left at
        /// max_blob_gas_per_block(blob_params). EIP-7840 blob schedule
        /// constants (target, max, base_fee_update_fraction) live in
        /// BCOS2Evmone.h so both this executor and blockHeaderToBlockInfo
        /// share them.
        task::Task<void> prepare()
        {
            auto revOpt = ledgerConfig.get().evmcRevisionForBlock(blockHeader.get().number());
            if (!revOpt.has_value())
            {
                BOOST_THROW_EXCEPTION(
                    EvmcRevisionNotConfigured() << errinfo_comment("evmcRevision not configured"));
            }
            m_rev = *revOpt;
            m_blockInfo = blockHeaderToBlockInfo(blockHeader.get(), ledgerConfig.get(), m_rev);
            m_evmTx = bcosTransactionToEvmone(transaction.get());
            m_validationError.reset();
            m_blobParams = blobParamsForRevision(m_rev);
            m_blobGasLeft =
                static_cast<int64_t>(evmone::state::max_blob_gas_per_block(m_blobParams));
            co_return;
        }

        /// Phase 2 — validate & execute.
        ///
        /// Runs evmone's validate_transaction() against the current storage
        /// state, then — if valid — transition() and immediately applies the
        /// returned StateDiff back to the storage.
        ///
        /// Validation happens here, once per transaction, against the state the
        /// transaction actually executes on. It must not live in prepare():
        /// the schedulers batch prepare() for a whole chunk before any
        /// execute(), so validating there would run every transaction against
        /// the pre-chunk state. Both directions are wrong:
        ///   * a transaction invalid at prepare() (e.g. nonce ahead of an
        ///     earlier same-chunk transaction) can be valid by the time it
        ///     executes, and
        ///   * a transaction valid at prepare() but invalidated by an earlier
        ///     same-chunk transaction (same sender, same nonce; or a sender
        ///     drained by an earlier transaction) must be rejected, not
        ///     executed: evmone's transition() trusts its caller and does not
        ///     re-check nonce/balance (state.cpp bumps the sender nonce and
        ///     subtracts the max cost unconditionally, and the balance is an
        ///     intx::uint256 that would wrap).
        /// A transaction that fails this validation is genuinely invalid: it is
        /// skipped (no state written) and finish() emits a failure receipt.
        ///
        /// Writing the diff here — not in finish() — is what gives transactions
        /// within a chunk sequential read-through semantics, matching
        /// TransactionExecutorImpl: both schedulers batch each phase across a
        /// whole chunk, so if the diff were only applied in finish(), every
        /// transaction in the chunk would execute against the same unmodified
        /// state (and, since the diffs are absolute post-state values, later
        /// diffs would silently discard earlier ones). Reads through the state
        /// view are synchronously bridged with task::tbb::syncWait.
        task::Task<void> execute()
        {
            auto validationResult =
                evmone::state::validate_transaction(m_stateView, m_blockInfo, m_evmTx, m_rev,
                    m_blockInfo.gas_limit /*block_gas_left*/, m_blobGasLeft /*blob_gas_left*/);
            if (auto* props = std::get_if<evmone::state::TransactionProperties>(&validationResult))
            {
                // Valid against the current state — use evmone's computed properties.
                m_txProps = *props;
            }
            else
            {
                // Genuinely invalid — skip execution; finish() produces a
                // failure receipt and nothing is written for this transaction.
                // NOTE: we do not special-case SENDER_NOT_EOA here. EIP-3607
                // requires rejecting transactions whose sender has non-delegating
                // code, and EEST tests check this (e.g.
                // test_set_code_from_account_with_non_delegating_code). Only
                // accounts with empty code or a 0xef0100 delegation designator
                // may be senders, which evmone's validate_transaction already
                // accepts.
                m_validationError = std::get<std::error_code>(validationResult);
                co_return;
            }

            m_evmReceipt = evmone::state::transition(m_stateView, m_blockInfo,
                executor.get().blockHashes(), m_evmTx, m_rev, executor.get().vm(), m_txProps);

            // Apply the resulting state diff immediately so later transactions
            // in the same chunk observe it.
            co_await applyStateDiff(
                storage.get(), m_evmReceipt.state_diff, m_rev, *executor.get().m_hashImpl);
            co_return;
        }

        /// Phase 3 — finalize.
        ///
        /// Produces the BCOS receipt. The state was already applied in
        /// execute(); this phase only converts the evmone receipt. The one
        /// exception is a transaction that failed validation in execute(): it
        /// gets a failure receipt and no state was written for it.
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            if (m_validationError.has_value())
            {
                co_return validationErrorReceipt(*m_validationError,
                    executor.get().m_receiptFactory, blockHeader.get().number());
            }
            co_return evmoneReceiptToBcos(
                m_evmReceipt, executor.get().m_receiptFactory, blockHeader.get().number());
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
    crypto::Hash::Ptr m_hashImpl;
    evmc::VM m_vm;
    std::shared_ptr<evmone::state::BlockHashes> m_blockHashes;
    ZeroBlockHashes m_zeroBlockHashes;
};

}  // namespace bcos::executor_v1::eth
