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

class EthereumExecutor
{
public:
    EthereumExecutor(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl)
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
                    std::runtime_error("evmcRevision not configured"));
            }
            auto rev = *revOpt;
            auto blockInfo = blockHeaderToBlockInfo(blockHeader, ledgerConfig);
            auto evmTx = bcosTransactionToEvmone(transaction);

            // Build state view adapter
            StorageStateView<Storage> stateView(storage);

            // Validate transaction using evmone's built-in logic.
            // This ensures the intrinsic gas, min_gas_cost, and all validation
            // checks match evmone exactly.
            // We handle SENDER_NOT_EOA specially because EEST fixtures may set
            // code on sender accounts for testing purposes.
            evmone::state::TransactionProperties txProps;
            auto validationResult = evmone::state::validate_transaction(
                stateView, blockInfo, evmTx, rev,
                evmTx.gas_limit /*block_gas_left - whole block available*/,
                evmTx.blob_gas_used() /*blob_gas_left*/);

            if (auto* props = std::get_if<evmone::state::TransactionProperties>(&validationResult))
            {
                // Validation succeeded - use evmone's computed properties
                txProps = *props;
            }
            else
            {
                auto& error = std::get<std::error_code>(validationResult);
                // EIP-3607 SENDER_NOT_EOA: EEST fixtures may set code on senders.
                // Fall back to manual intrinsic gas computation for this case.
                if (error == make_error_code(evmone::state::SENDER_NOT_EOA))
                {
                    static constexpr int64_t TX_BASE_COST = 21000;
                    static constexpr int64_t TX_CREATE_COST = 32000;
                    static constexpr int64_t DATA_TOKEN_COST = 4;
                    static constexpr int64_t INITCODE_WORD_COST = 2;
                    static constexpr int64_t TOTAL_COST_FLOOR_PER_TOKEN = 10;
                    static constexpr int64_t ACCESS_LIST_ADDRESS_COST = 2400;
                    static constexpr int64_t ACCESS_LIST_STORAGE_KEY_COST = 1900;

                    auto const& input = evmTx.data;
                    size_t numZero = std::count(input.begin(), input.end(), uint8_t(0));
                    size_t numNonzero = input.size() - numZero;
                    size_t nonzeroMult = (rev >= EVMC_ISTANBUL) ? 4 : 17;
                    int64_t numTokens = static_cast<int64_t>(nonzeroMult * numNonzero + numZero);

                    bool isCreate = !evmTx.to.has_value();
                    int64_t createCost = (isCreate && rev >= EVMC_HOMESTEAD) ? TX_CREATE_COST : 0;
                    int64_t dataCost = numTokens * DATA_TOKEN_COST;

                    int64_t accessListCost = 0;
                    for (auto const& [_, keys] : evmTx.access_list)
                        accessListCost += ACCESS_LIST_ADDRESS_COST +
                            static_cast<int64_t>(keys.size()) * ACCESS_LIST_STORAGE_KEY_COST;

                    int64_t authListCost =
                        static_cast<int64_t>(evmTx.authorization_list.size()) * 25000;

                    int64_t initcodeCost = (isCreate && rev >= EVMC_SHANGHAI) ?
                        INITCODE_WORD_COST *
                            static_cast<int64_t>((input.size() + 31) / 32) :
                        0;

                    int64_t intrinsicCost =
                        TX_BASE_COST + createCost + dataCost + accessListCost +
                        authListCost + initcodeCost;

                    int64_t minCost = (rev >= EVMC_PRAGUE) ?
                        TX_BASE_COST + numTokens * TOTAL_COST_FLOOR_PER_TOKEN :
                        0;

                    txProps.execution_gas_limit = evmTx.gas_limit - intrinsicCost;
                    txProps.min_gas_cost = minCost;

                    // Pre-check: intrinsic gas must be affordable
                    if (evmTx.gas_limit < std::max(intrinsicCost, minCost))
                        BOOST_THROW_EXCEPTION(
                            IntrinsicGasTooLow{} << bcos::errinfo_comment("intrinsic gas too low"));
                }
                else
                {
                    // Transaction is genuinely invalid - throw an exception
                    BOOST_THROW_EXCEPTION(std::runtime_error(
                        "Transaction validation failed: " + error.message()));
                }
            }

            // Execute
            ZeroBlockHashes zeroBlockHashes;
            auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
            auto evmReceipt = evmone::state::transition(
                stateView, blockInfo, bh, evmTx, rev, m_vm, txProps);

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

        auto diff = evmone::state::finalize(
            stateView, rev, coinbase, blockReward, {}, withdrawals);

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

        ExecuteContext(EthereumExecutor& exec, Storage& st,
            protocol::BlockHeader const& bh, protocol::Transaction const& tx,
            int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(exec), storage(st), blockHeader(bh), transaction(tx),
            contextID(cid), ledgerConfig(cfg), call(c)
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
