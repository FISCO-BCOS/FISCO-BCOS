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
    //   - m_blockHashes   : const noexcept lookups; StorageBlockHashes is
    //                       internally mutex-protected, ZeroBlockHashes is
    //                       stateless.
    template <class Storage>
    struct ExecuteContext
    {
        std::reference_wrapper<EthereumExecutor> executor;
        std::reference_wrapper<Storage> storage;
        std::reference_wrapper<protocol::BlockHeader const> blockHeader;
        std::reference_wrapper<protocol::Transaction const> transaction;
        int contextID;
        std::reference_wrapper<ledger::LedgerConfig const> ledgerConfig;
        bool call;

        // Per-phase state — owned by this context, safe for concurrent phases.
        evmc_revision m_rev = EVMC_FRONTIER;
        evmone::state::BlockInfo m_blockInfo;
        evmone::state::Transaction m_evmTx;
        StorageStateView<Storage> m_stateView;
        evmone::state::TransactionProperties m_txProps;
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

        /// Phase 1 — validation & pre-state snapshot.
        ///
        /// Converts the BCOS types to evmone types and runs evmone's
        /// validate_transaction() against the read-only StorageStateView
        /// (created in the constructor over the current storage). No state is
        /// written in this phase, so it only reads the storage and can run
        /// concurrently with other contexts' phases.
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

            // Validate transaction using evmone's built-in logic.
            // This ensures the intrinsic gas, min_gas_cost, and all validation
            // checks match evmone exactly.
            // blob_gas_left is the block's remaining blob gas (max_blob_gas_per_block
            // minus already-included blobs). It must NOT be the tx's own blob gas,
            // otherwise a tx with too many blobs would pass validation. Matches
            // evmone's t8n/blockchaintest runners which start blob_gas_left at
            // max_blob_gas_per_block(blob_params).
            // EIP-7840 blob schedule constants (target, max, base_fee_update_fraction).
            constexpr evmone::state::BlobParams PRAGUE_BLOB_PARAMS{
                .target = 6, .max = 9, .base_fee_update_fraction = 5007716};
            constexpr evmone::state::BlobParams CANCUN_BLOB_PARAMS{
                .target = 3, .max = 6, .base_fee_update_fraction = 3338477};

            evmone::state::BlobParams blobParams{};
            if (m_rev >= EVMC_PRAGUE)
            {
                // EIP-7840 blob schedule: Prague/Osaka.
                blobParams = PRAGUE_BLOB_PARAMS;
            }
            else if (m_rev == EVMC_CANCUN)
            {
                // EIP-7840 blob schedule: Cancun.
                blobParams = CANCUN_BLOB_PARAMS;
            }
            const auto blobGasLeft =
                static_cast<int64_t>(evmone::state::max_blob_gas_per_block(blobParams));
            auto validationResult =
                evmone::state::validate_transaction(m_stateView, m_blockInfo, m_evmTx, m_rev,
                    m_blockInfo.gas_limit /*block_gas_left*/, blobGasLeft /*blob_gas_left*/);

            if (auto* props = std::get_if<evmone::state::TransactionProperties>(&validationResult))
            {
                // Validation succeeded - use evmone's computed properties
                m_txProps = *props;
            }
            else
            {
                auto& error = std::get<std::error_code>(validationResult);
                // Transaction is invalid - throw an exception.
                // NOTE: we no longer special-case SENDER_NOT_EOA here. EIP-3607
                // requires rejecting transactions whose sender has non-delegating
                // code, and EEST tests check this (e.g.
                // test_set_code_from_account_with_non_delegating_code). Only
                // accounts with empty code or a 0xef0100 delegation designator
                // may be senders, which evmone's validate_transaction already
                // accepts.
                BOOST_THROW_EXCEPTION(TransactionValidationFailed() << errinfo_comment(
                                          "Transaction validation failed: " + error.message()));
            }
            co_return;
        }

        /// Phase 2 — EVM execution.
        ///
        /// Runs evmone's transition() against the pre-state snapshot produced
        /// in prepare(). Reads through the state view are synchronously
        /// bridged with task::tbb::syncWait. No state is written here either;
        /// the resulting StateDiff is only recorded in this context and is
        /// applied to storage in finish().
        task::Task<void> execute()
        {
            m_evmReceipt = evmone::state::transition(m_stateView, m_blockInfo,
                executor.get().blockHashes(), m_evmTx, m_rev, executor.get().vm(), m_txProps);
            co_return;
        }

        /// Phase 3 — persist & finalize.
        ///
        /// Applies the StateDiff produced in execute() back to the storage and
        /// converts the evmone receipt to a BCOS receipt. This is the only
        /// phase that writes to storage.
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            co_await applyStateDiff(
                storage.get(), m_evmReceipt.state_diff, m_rev, *executor.get().m_hashImpl);
            co_return evmoneReceiptToBcos(
                m_evmReceipt, executor.get().m_receiptFactory, blockHeader.get().number());
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return ExecuteContext<std::decay_t<decltype(storage)>>{
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
