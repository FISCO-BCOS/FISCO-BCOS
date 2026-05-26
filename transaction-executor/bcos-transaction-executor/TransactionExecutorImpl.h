#pragma once

#include "RollbackableStorage.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Exceptions.h"
#include "precompiled/PrecompiledManager.h"
#include "vm/HostContext.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>

namespace bcos::executor_v1
{
#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

DERIVE_BCOS_EXCEPTION(InvalidReceiptVersion);

evmc_message newEVMCMessage(protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, PrecompiledManager& precompiledManager);

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    std::reference_wrapper<PrecompiledManager> m_precompiledManager;

    using TransientStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

    // FIB-75: Effective gas limit for EVM execution.
    // When bugfix_gas_payment_balance_precheck is enabled and the tx declares gasLimit > 0,
    // the EVM budget is capped at tx.gasLimit() (geth-compatible); otherwise falls back to
    // blockGasLimit for backward compat.
    static int64_t computeEffectiveGasLimit(
        protocol::Transaction const& tx, ledger::LedgerConfig const& cfg)
    {
        const auto txSysGasLimit = static_cast<int64_t>(std::get<0>(cfg.gasLimit()));
        if (cfg.features().get(ledger::Features::Flag::bugfix_gas_payment_balance_precheck) &&
            tx.gasLimit() > 0)
        {
            return std::min<int64_t>(tx.gasLimit(), txSysGasLimit);
        }
        return txSysGasLimit;
    }

    template <class Storage>
    struct ExecuteContext
    {
        struct Data
        {
            std::reference_wrapper<TransactionExecutorImpl> m_executor;
            std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
            std::reference_wrapper<protocol::Transaction const> m_transaction;
            int m_contextID;
            std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
            Rollbackable<Storage> m_rollbackableStorage;
            Rollbackable<Storage>::Savepoint m_startSavepoint;
            // FIB-75: savepoint right after buyGas() pre-deduction — used to rollback only EVM
            // effects while preserving the pre-deducted balance.
            Rollbackable<Storage>::Savepoint m_afterBuyGasSavepoint{0};
            TransientStorage m_transientStorage;
            Rollbackable<decltype(m_transientStorage)> m_rollbackableTransientStorage;
            bool m_call;
            int64_t m_gasUsed = 0;
            std::string m_gasPriceStr;

            int64_t m_gasLimit;
            int64_t m_seq = 0;
            evmc_address m_origin;
            u256 m_nonce;
            hostcontext::HostContext<decltype(m_rollbackableStorage),
                decltype(m_rollbackableTransientStorage)>
                m_hostContext;
            std::optional<EVMCResult> m_evmcResult;

            Data(TransactionExecutorImpl& executor, Storage& storage,
                protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
                int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
              : m_executor(executor),
                m_blockHeader(blockHeader),
                m_transaction(transaction),
                m_contextID(contextID),
                m_ledgerConfig(ledgerConfig),
                m_rollbackableStorage(storage),
                m_startSavepoint(m_rollbackableStorage.current()),
                m_rollbackableTransientStorage(m_transientStorage),
                m_call(call),
                m_gasLimit(computeEffectiveGasLimit(transaction, ledgerConfig)),
                m_origin((!m_transaction.get().sender().empty() &&
                             m_transaction.get().sender().size() == sizeof(evmc_address)) ?
                             *(evmc_address*)m_transaction.get().sender().data() :
                             evmc_address{}),
                m_nonce(hex2u(transaction.nonce())),
                m_hostContext(m_rollbackableStorage, m_rollbackableTransientStorage, blockHeader,
                    newEVMCMessage(m_blockHeader.get().number(), transaction, m_gasLimit, m_origin),
                    m_origin, transaction.abi(), contextID, m_seq, executor.m_precompiledManager,
                    ledgerConfig, *executor.m_hashImpl, transaction.type() != 0, m_nonce,
                    task::syncWait)
            {}
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (step == 0)
            {
                co_await m_data->m_hostContext.prepare();
            }
            else if constexpr (step == 1)
            {
                auto updated = co_await updateNonce();
                if (updated)
                {
                    m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();
                }

                if (const auto gasPrice =
                        u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                    m_data->m_transaction.get().type() == 1 &&  // web3Tx
                    m_data->m_ledgerConfig.get().features().get(
                        ledger::Features::Flag::bugfix_gas_payment_balance_precheck) &&
                    gasPrice > 0)
                {
                    // FIB-75 geth-style: buy gas (pre-deduct), execute, refund unused gas.
                    if (!co_await buyGas())
                    {
                        co_return {};
                    }
                    m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
                    co_await refundGas();
                }
                else
                {
                    // Legacy path
                    m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
                    co_await consumeBalance();
                }
            }
            else if constexpr (step == 2)
            {
                co_return co_await finish();
            }

            co_return {};
        }

        task::Task<bool> updateNonce()
        {
            if (const auto& transaction = m_data->m_transaction.get();
                transaction.type() == 1)  // 1 = web3
                                          // transaction
            {
                auto& callNonce = m_data->m_nonce;
                ledger::account::EVMAccount account(m_data->m_rollbackableStorage, m_data->m_origin,
                    m_data->m_ledgerConfig.get().features().get(
                        ledger::Features::Flag::feature_raw_address));

                if (!co_await account.exists())
                {
                    co_await account.create();
                }
                auto nonceInStorage = co_await account.nonce();
                auto storageNonce = u256(nonceInStorage.value_or("0"));
                u256 newNonce = std::max(callNonce, storageNonce) + 1;
                co_await account.setNonce(newNonce.convert_to<std::string>());
                co_return true;
            }
            co_return false;
        }

        // FIB-75 (geth-style): Pre-deduct gasLimit * gasPrice from sender before EVM execution.
        // If balance is insufficient to cover gas + value, fail immediately (EVM does not run,
        // no balance deducted, nonce preserved as replay protection).
        // On success, balance -= gasLimit * gasPrice; EVM then runs with m_gasLimit as its
        // gas budget, guaranteeing gasUsed <= gasLimit so refundGas() always has enough to
        // settle without confiscating extra balance.
        task::Task<bool> buyGas()
        {
            if (m_data->m_call)
            {
                co_return true;
            }

            // FIB-75: use the transaction's effective gas price (legacy gasPrice or EIP-1559
            // maxFeePerGas) so charging matches what the txpool validated.
            const auto gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
            if (gasPrice == 0)
            {
                co_return true;
            }

            if (m_data->m_gasLimit <= 0)
            {
                co_return true;
            }

            const auto maxGasCost = u256(m_data->m_gasLimit) * gasPrice;
            const auto txValue = u256(m_data->m_transaction.get().value());
            const auto totalRequired = maxGasCost + txValue;

            auto& evmcMessage = m_data->m_hostContext.message();
            auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
            auto senderBalance = co_await senderAccount.balance();

            if (senderBalance < totalRequired)
            {
                TRANSACTION_EXECUTOR_LOG(ERROR)
                    << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                    << LOG_KV("maxGasCost", maxGasCost) << LOG_KV("txValue", txValue)
                    << LOG_KV("totalRequired", totalRequired);

                // FIB-75: charge minimum penalty = min(balance, intrinsic_gas * gasPrice).
                // The transaction is already in a consensus-packed block and consumed
                // consensus/storage resources, so a sender who can't cover full gas cost
                // still pays at least the 21000-gas base cost (geth's intrinsic gas for
                // an empty tx). If balance < intrinsic cost, drain what's left. This
                // prevents free spam from repeatedly submitting under-funded transactions.
                constexpr static int64_t INTRINSIC_GAS = 21000;
                const auto intrinsicCost = u256(INTRINSIC_GAS) * gasPrice;
                const auto penalty = std::min(senderBalance, intrinsicCost);
                if (penalty > 0)
                {
                    co_await senderAccount.setBalance(senderBalance - penalty);
                }

                evmc_result failResult{};
                failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                failResult.gas_left = 0;
                failResult.output_data = nullptr;
                failResult.output_size = 0;
                failResult.release = nullptr;
                failResult.create_address = {};
                m_data->m_evmcResult.emplace(
                    EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
                // gasUsed reflects what was actually charged as penalty (in gas units).
                m_data->m_gasUsed = (penalty / gasPrice).template convert_to<int64_t>();
                m_data->m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);

                co_return false;
            }

            // Pre-deduct max gas cost from sender
            co_await senderAccount.setBalance(senderBalance - maxGasCost);
            m_data->m_afterBuyGasSavepoint = m_data->m_rollbackableStorage.current();
            m_data->m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
            co_return true;
        }

        // FIB-75 (geth-style): After EVM execution, refund (gasLimit - gasUsed) * gasPrice.
        // If EVM failed (non-SUCCESS, non-REVERT), roll back state changes while preserving
        // the pre-deducted gas cost. gasUsed <= gasLimit is guaranteed because the EVM's
        // gas budget is m_gasLimit.
        task::Task<void> refundGas()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;

            if (m_data->m_call)
            {
                co_return;
            }

            // FIB-75: mirror buyGas() — use the tx's effective gas price.
            const auto gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
            if (gasPrice == 0)
            {
                co_return;
            }

            // On EVM failure (not SUCCESS / REVERT), rollback EVM state changes but keep
            // the pre-deducted gas — the sender still pays for the wasted execution.
            if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
            {
                co_await m_data->m_rollbackableStorage.rollback(m_data->m_afterBuyGasSavepoint);
            }

            // Refund unused gas
            if (evmcResult.gas_left > 0)
            {
                auto refund = u256(evmcResult.gas_left) * gasPrice;
                auto& evmcMessage = m_data->m_hostContext.message();
                auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
                auto balance = co_await senderAccount.balance();
                co_await senderAccount.setBalance(balance + refund);
            }
        }

        // Legacy balance consumption — only used when bugfix_gas_payment_balance_precheck is OFF.
        // Kept unchanged from pre-FIB-75 behavior: on insufficient balance, rollback execution
        // effects and deduct nothing (the original FIB-75 bug that's fixed by the precheck flag).
        task::Task<void> consumeBalance()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
            if (!m_data->m_call)
            {
                auto& evmcMessage = m_data->m_hostContext.message();
                if (auto gasPrice = u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                    gasPrice > 0)
                {
                    constexpr static const auto GAS_PRICE_DIGITS = 256;
                    m_data->m_gasPriceStr =
                        "0x" + gasPrice.str(GAS_PRICE_DIGITS, std::ios_base::hex);

                    auto balanceUsed = m_data->m_gasUsed * gasPrice;
                    auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
                    auto senderBalance = co_await senderAccount.balance();

                    if (senderBalance < balanceUsed)
                    {
                        TRANSACTION_EXECUTOR_LOG(ERROR) << "Insufficient balance: " << senderBalance
                                                        << ", balanceUsed: " << balanceUsed;
                        evmcResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                        evmcResult.status = protocol::TransactionStatus::NotEnoughCash;
                        if (evmcResult.release != nullptr)
                        {
                            evmcResult.release(std::addressof(evmcResult));
                        }
                        evmcResult.output_data = nullptr;
                        evmcResult.output_size = 0;
                        evmcResult.release = nullptr;
                        evmcResult.create_address = {};
                        co_await m_data->m_rollbackableStorage.rollback(m_data->m_startSavepoint);
                    }
                    else
                    {
                        co_await senderAccount.setBalance(senderBalance - balanceUsed);
                    }
                }
            }
        }

        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            const auto& evmcMessage = m_data->m_hostContext.message();
            auto& evmcResult = *m_data->m_evmcResult;

            std::string newContractAddress;
            if (evmcMessage.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
            {
                newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
                boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                    evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                    std::back_inserter(newContractAddress));
            }

            if (evmcResult.status_code != EVMC_SUCCESS)
            {
                TRANSACTION_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;

                auto [outputData, outputSize, release] = fillErrorOutputInPlace(
                    *m_data->m_executor.get().m_hashImpl, evmcResult.status_code);
                if (release != nullptr)
                {
                    if (evmcResult.release != nullptr)
                    {
                        evmcResult.release(std::addressof(evmcResult));
                    }
                    evmcResult.output_data = outputData;
                    evmcResult.output_size = outputSize;
                    evmcResult.release = release;
                }
                if (m_data->m_ledgerConfig.get().features().get(
                        ledger::Features::Flag::bugfix_revert_logs))
                {
                    m_data->m_hostContext.logs().clear();
                }
            }

            auto receiptStatus = static_cast<int32_t>(evmcResult.status);
            auto const& logEntries = m_data->m_hostContext.logs();
            protocol::TransactionReceipt::Ptr receipt;
            switch (auto transactionVersion = static_cast<bcos::protocol::TransactionVersion>(
                        m_data->m_transaction.get().version()))
            {
            case bcos::protocol::TransactionVersion::V0_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt(
                    m_data->m_gasUsed, std::move(newContractAddress), logEntries, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number());
                break;
            case bcos::protocol::TransactionVersion::V1_VERSION:
            case bcos::protocol::TransactionVersion::V2_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt2(
                    m_data->m_gasUsed, std::move(newContractAddress), logEntries, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number(), std::move(m_data->m_gasPriceStr),
                    transactionVersion);
                break;
            default:
                BOOST_THROW_EXCEPTION(InvalidReceiptVersion{} << bcos::errinfo_comment(
                                          "Invalid receipt version: " +
                                          std::to_string(m_data->m_transaction.get().version())));
            }

            TRANSACTION_EXECUTOR_LOG(TRACE) << "Execute transaction finished: " << *receipt;
            co_return receipt;  // 完成第三步 Complete the third step
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        TRANSACTION_EXECUTOR_LOG(TRACE) << "Create transaction context: " << transaction;
        co_return {*this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executeContext.template executeStep<0>();
        co_await executeContext.template executeStep<1>();
        co_return co_await executeContext.template executeStep<2>();
    }
};

}  // namespace bcos::executor_v1
