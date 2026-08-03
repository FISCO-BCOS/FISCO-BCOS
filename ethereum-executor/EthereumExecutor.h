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
    EthereumExecutor(
        protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl)
      : m_receiptFactory(receiptFactory),
        m_hashImpl(std::move(hashImpl)),
        m_vm(evmc_create_evmone())
    {}

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// Execute a single Ethereum transaction.
    ///
    /// Flow:
    ///   1. Create a StorageStateView from the storage (read-only pre-state)
    ///   2. Convert BCOS types to evmone types
    ///   3. Call evmone::state::validate_transaction() for validation
    ///   4. Call evmone::state::transition() for EVM execution
    ///   5. Apply the returned StateDiff back to storage
    ///   6. Convert evmone receipt to BCOS receipt
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        (void)contextID;  // not used in Ethereum mode
        (void)call;       // EEST tests always use call=false (real execution)

        try
        {
            auto revOpt = ledgerConfig.evmcRevisionForBlock(blockHeader.number());
            if (!revOpt.has_value())
            {
                BOOST_THROW_EXCEPTION(
                    EvmcRevisionNotConfigured() << errinfo_comment("evmcRevision not configured"));
            }
            auto rev = *revOpt;
            auto blockInfo = blockHeaderToBlockInfo(blockHeader, ledgerConfig, rev);
            auto evmTx = bcosTransactionToEvmone(transaction);

            // Build state view adapter
            StorageStateView<Storage> stateView(storage);

            // Validate transaction using evmone's built-in logic.
            // This ensures the intrinsic gas, min_gas_cost, and all validation
            // checks match evmone exactly.
            // blob_gas_left is the block's remaining blob gas (max_blob_gas_per_block
            // minus already-included blobs). It must NOT be the tx's own blob gas,
            // otherwise a tx with too many blobs would pass validation. Matches
            // evmone's t8n/blockchaintest runners which start blob_gas_left at
            // max_blob_gas_per_block(blob_params).
            evmone::state::TransactionProperties txProps;
            evmone::state::BlobParams blobParams{};
            if (rev >= EVMC_PRAGUE)
            {
                // EIP-7840 blob schedule: Prague/Osaka (target=6, max=9).
                blobParams = {.target = 6, .max = 9, .base_fee_update_fraction = 5007716};
            }
            else if (rev == EVMC_CANCUN)
            {
                // EIP-7840 blob schedule: Cancun (target=3, max=6).
                blobParams = {.target = 3, .max = 6, .base_fee_update_fraction = 3338477};
            }
            const auto blobGasLeft =
                static_cast<int64_t>(evmone::state::max_blob_gas_per_block(blobParams));
            auto validationResult = evmone::state::validate_transaction(stateView, blockInfo, evmTx,
                rev, blockInfo.gas_limit /*block_gas_left*/, blobGasLeft /*blob_gas_left*/);

            if (auto* props = std::get_if<evmone::state::TransactionProperties>(&validationResult))
            {
                // Validation succeeded - use evmone's computed properties
                txProps = *props;
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

            // Execute
            ZeroBlockHashes zeroBlockHashes;
            auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
            auto evmReceipt =
                evmone::state::transition(stateView, blockInfo, bh, evmTx, rev, m_vm, txProps);

            // Apply state diff back to storage
            co_await applyStateDiff(storage, evmReceipt.state_diff, rev, *m_hashImpl);

            // Convert receipt
            co_return evmoneReceiptToBcos(evmReceipt, m_receiptFactory, blockHeader.number());
        }
        catch (std::exception const& e)
        {
            throw;
        }
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
        auto const& cb = blockHeader.coinbase();
        if (cb.size() == sizeof(evmc_address))
            std::copy_n(cb.begin(), sizeof(evmc_address), coinbase.bytes);

        auto diff = evmone::state::finalize(stateView, rev, coinbase, blockReward, {}, withdrawals);

        co_await applyStateDiff(storage, diff, rev, *m_hashImpl);
    }

    // TransactionExecutor concept support: ExecuteContext
    template <class Storage>
    struct ExecuteContext
    {
        EthereumExecutor& executor;
        Storage& storage;
        protocol::BlockHeader const& blockHeader;
        protocol::Transaction const& transaction;
        int contextID;
        ledger::LedgerConfig const& ledgerConfig;
        bool call;

        ExecuteContext(EthereumExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c)
        {}
    };

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

private:
    protocol::TransactionReceiptFactory const& m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::eth
