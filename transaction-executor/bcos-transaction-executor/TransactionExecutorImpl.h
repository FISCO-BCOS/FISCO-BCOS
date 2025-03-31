#pragma once

#include "RollbackableStorage.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/Wait.h"
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

DERIVE_BCOS_EXCEPTION(InvalidReceiptVersion);

namespace bcos::executor_v1
{
#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

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

    template <class Storage>
    struct ExecuteContext
    {
        std::reference_wrapper<TransactionExecutorImpl> m_executor;
        std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
        std::reference_wrapper<protocol::Transaction const> m_transaction;
        int m_contextID;
        std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
        Rollbackable<Storage> m_rollbackableStorage;
        Rollbackable<Storage>::Savepoint m_startSavepoint;
        TransientStorage m_transientStorage;
        Rollbackable<decltype(m_transientStorage)> m_rollbackableTransientStorage;
        bool m_call;

        int64_t m_gasLimit;
        int64_t m_seq = 0;
        evmc_address m_origin;
        u256 m_nonce;
        hostcontext::HostContext<decltype(m_rollbackableStorage),
            decltype(m_rollbackableTransientStorage)>
            m_hostContext;
        std::optional<EVMCResult> m_evmcResult;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_executor(executor),
            m_blockHeader(blockHeader),
            m_transaction(transaction),
            m_contextID(contextID),
            m_ledgerConfig(ledgerConfig),
            m_rollbackableStorage(storage),
            m_startSavepoint(current(m_rollbackableStorage)),
            m_rollbackableTransientStorage(m_transientStorage),
            m_call(call),
            m_gasLimit(static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()))),
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

    friend auto tag_invoke(tag_t<createExecuteContext> /*unused*/,
        TransactionExecutorImpl& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<std::unique_ptr<ExecuteContext<std::decay_t<decltype(storage)>>>>
    {
        TRANSACTION_EXECUTOR_LOG(TRACE) << "Create transaction context: " << transaction;
        co_return std::make_unique<ExecuteContext<std::decay_t<decltype(storage)>>>(
            executor, storage, blockHeader, transaction, contextID, ledgerConfig, call);
    }

    template <int step>
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<executeStep> /*unused*/, auto& context)
    {
        auto& executeContext = *context;

        if constexpr (step == 0)
        {
            co_await updateNonce(executeContext);
            co_await executeContext.m_hostContext.prepare();
        }
        else if constexpr (step == 1)
        {
            executeContext.m_evmcResult.emplace(co_await executeContext.m_hostContext.execute());
        }
        else if constexpr (step == 2)
        {
            co_return co_await finish(executeContext);
        }

        co_return {};
    }

    friend task::Task<void> updateNonce(auto& executeContext)
    {
        if (auto& transaction = executeContext.m_transaction.get();
            transaction.type() == 1)  // 1 = web3 transaction
        {
            auto& callNonce = executeContext.m_nonce;
            ledger::account::EVMAccount account(executeContext.m_rollbackableStorage,
                executeContext.m_origin,
                executeContext.m_ledgerConfig.get().features().get(
                    ledger::Features::Flag::feature_raw_address));

            if (!co_await ledger::account::exists(account))
            {
                co_await ledger::account::create(account);
            }
            auto nonceInStorage = co_await ledger::account::nonce(account);
            auto storageNonce = u256(nonceInStorage.value_or("0"));
            u256 newNonce = std::max(callNonce, storageNonce) + 1;
            co_await ledger::account::setNonce(account, newNonce.convert_to<std::string>());
        }
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> finish(auto& executeContext)
    {
        const auto& evmcMessage = executeContext.m_hostContext.message();
        auto& evmcResult = *executeContext.m_evmcResult;

        std::string newContractAddress;
        if (evmcMessage.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
        {
            newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
            boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                std::back_inserter(newContractAddress));
        }

        if (evmcResult.status_code != 0)
        {
            TRANSACTION_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;

            auto [_, errorMessage] = evmcStatusToErrorMessage(
                *executeContext.m_executor.get().m_hashImpl, evmcResult.status_code);
            if (!errorMessage.empty())
            {
                auto output = std::make_unique_for_overwrite<uint8_t[]>(errorMessage.size());
                std::uninitialized_copy(errorMessage.begin(), errorMessage.end(), output.get());
                evmcResult.output_data = output.release();
                evmcResult.output_size = errorMessage.size();
                evmcResult.release = [](const struct evmc_result* result) {
                    delete[] result->output_data;
                };
            }
        }

        std::string gasPriceStr;
        auto gasUsed = executeContext.m_gasLimit - evmcResult.gas_left;
        if (executeContext.call)
        {
            if (auto gasPrice = u256{std::get<0>(executeContext.m_ledgerConfig.get().gasPrice())};
                gasPrice > 0)
            {
                gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);

                auto balanceUsed = gasUsed * gasPrice;
                auto senderAccount = getAccount(executeContext.m_hostContext, evmcMessage.sender);
                auto senderBalance = co_await ledger::account::balance(senderAccount);

                if (senderBalance < balanceUsed)
                {
                    TRANSACTION_EXECUTOR_LOG(ERROR) << "Insufficient balance: " << senderBalance
                                                    << ", balanceUsed: " << balanceUsed;
                    evmcResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                    evmcResult.status = protocol::TransactionStatus::NotEnoughCash;
                    co_await rollback(
                        executeContext.m_rollbackableStorage, executeContext.m_startSavepoint);
                }
                else
                {
                    co_await ledger::account::setBalance(
                        senderAccount, senderBalance - balanceUsed);
                }
            }
        }

        auto receiptStatus = static_cast<int32_t>(evmcResult.status);
        auto const& logEntries = executeContext.m_hostContext.logs();
        protocol::TransactionReceipt::Ptr receipt;
        switch (auto transactionVersion = static_cast<bcos::protocol::TransactionVersion>(
                    executeContext.m_transaction.get().version()))
        {
        case bcos::protocol::TransactionVersion::V0_VERSION:
            receipt = executeContext.m_executor.get().m_receiptFactory.get().createReceipt(gasUsed,
                std::move(newContractAddress), logEntries, receiptStatus,
                {evmcResult.output_data, evmcResult.output_size},
                executeContext.m_blockHeader.get().number());
            break;
        case bcos::protocol::TransactionVersion::V1_VERSION:
        case bcos::protocol::TransactionVersion::V2_VERSION:
            receipt = executeContext.m_executor.get().m_receiptFactory.get().createReceipt2(gasUsed,
                std::move(newContractAddress), logEntries, receiptStatus,
                {evmcResult.output_data, evmcResult.output_size},
                executeContext.m_blockHeader.get().number(), std::move(gasPriceStr),
                transactionVersion);
            break;
        default:
            BOOST_THROW_EXCEPTION(
                InvalidReceiptVersion{} << bcos::errinfo_comment(
                    "Invalid receipt version: " +
                    std::to_string(executeContext.m_transaction.get().version())));
        }

        TRANSACTION_EXECUTOR_LOG(TRACE) << "Execte transaction finished: " << *receipt;
        co_return receipt;  // 完成第三步 Complete the third step
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<executeTransaction> /*unused*/, TransactionExecutorImpl& executor, auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call, auto&& syncWait)
    {
        auto executeContext = co_await createExecuteContext(
            executor, storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executor_v1::executeStep.operator()<0>(executeContext);
        co_await executor_v1::executeStep.operator()<1>(executeContext);
        co_return co_await executor_v1::executeStep.operator()<2>(executeContext);
    }
};

}  // namespace bcos::executor_v1
