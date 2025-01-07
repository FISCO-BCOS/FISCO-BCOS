#pragma once

#include "RollbackableStorage.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/Wait.h"
#include "precompiled/PrecompiledManager.h"
#include "vm/HostContext.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <functional>
#include <iterator>
#include <type_traits>

namespace bcos::transaction_executor
{
#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

evmc_message newEVMCMessage(protocol::Transaction const& transaction, int64_t gasLimit);

class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, PrecompiledManager& precompiledManager);

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    std::reference_wrapper<PrecompiledManager> m_precompiledManager;

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
        bcos::storage2::memory_storage::MemoryStorage<bcos::transaction_executor::StateKey,
            bcos::transaction_executor::StateValue, bcos::storage2::memory_storage::ORDERED>
            m_transientStorage;
        Rollbackable<decltype(m_transientStorage)> m_rollbackableTransientStorage;

        int64_t m_gasLimit;
        evmc_message m_evmcMessage;
        int64_t m_seq = 0;
        hostcontext::HostContext<decltype(m_rollbackableStorage),
            decltype(m_rollbackableTransientStorage)>
            m_hostContext;
        std::optional<EVMCResult> m_evmcResult;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig)
          : m_executor(executor),
            m_blockHeader(blockHeader),
            m_transaction(transaction),
            m_contextID(contextID),
            m_ledgerConfig(ledgerConfig),
            m_rollbackableStorage(storage),
            m_startSavepoint(current(m_rollbackableStorage)),
            m_rollbackableTransientStorage(m_transientStorage),
            m_gasLimit(static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()))),
            m_evmcMessage(newEVMCMessage(transaction, m_gasLimit)),
            m_hostContext(m_rollbackableStorage, m_rollbackableTransientStorage, blockHeader,
                m_evmcMessage, m_evmcMessage.sender, transaction.abi(), contextID, m_seq,
                executor.m_precompiledManager, ledgerConfig, *executor.m_hashImpl, task::syncWait)
        {
            if (blockHeader.number() == 0 &&
                m_transaction.get().to() == precompiled::AUTH_COMMITTEE_ADDRESS)
            {
                m_evmcMessage.kind = EVMC_CREATE;
            }
        }
    };

    friend auto tag_invoke(tag_t<createExecuteContext> /*unused*/,
        TransactionExecutorImpl& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig)
        -> task::Task<std::unique_ptr<ExecuteContext<std::decay_t<decltype(storage)>>>>
    {
        co_return std::make_unique<ExecuteContext<std::decay_t<decltype(storage)>>>(
            executor, storage, blockHeader, transaction, contextID, ledgerConfig);
    }

    template <int step>
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<executeStep> /*unused*/, auto& context)
    {
        auto& executeContext = *context;
        if constexpr (step == 0)
        {
            co_await prepare(executeContext.m_hostContext);
        }
        else if constexpr (step == 1)
        {
            executeContext.m_evmcResult.emplace(co_await execute(executeContext.m_hostContext));
        }
        else if constexpr (step == 2)
        {
            co_return co_await finish(executeContext);
        }

        co_return {};
    }


    friend task::Task<protocol::TransactionReceipt::Ptr> finish(auto& executeContext)
    {
        auto& evmcMessage = executeContext.m_evmcMessage;
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
                auto output = std::make_unique<uint8_t[]>(errorMessage.size());
                std::uninitialized_copy(errorMessage.begin(), errorMessage.end(), output.get());
                evmcResult.output_data = output.release();
                evmcResult.output_size = errorMessage.size();
                evmcResult.release =
                    +[](const struct evmc_result* result) { delete[] result->output_data; };
            }
        }

        auto gasUsed = executeContext.m_gasLimit - evmcResult.gas_left;

        if (executeContext.m_ledgerConfig.get().features().get(
                ledger::Features::Flag::feature_balance_policy1))
        {
            auto gasPrice = u256{std::get<0>(executeContext.m_ledgerConfig.get().gasPrice())};
            auto balanceUsed = gasUsed * gasPrice;
            auto senderAccount = getAccount(executeContext.m_hostContext, evmcMessage.sender);
            auto senderBalance = co_await ledger::account::balance(senderAccount);

            if (senderBalance < balanceUsed || senderBalance == 0)
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
                co_await ledger::account::setBalance(senderAccount, senderBalance - balanceUsed);
            }
        }

        // 如果本次调用是eoa调用系统合约，将gasUsed设置为0
        // If the call from eoa to system contract, the gasUsed is cleared to zero
        if (!executeContext.m_ledgerConfig.get().features().get(
                ledger::Features::Flag::bugfix_precompiled_gasused))
        {
            if (auto codeAddress = address2FixedArray(evmcMessage.code_address);
                precompiled::contains(bcos::precompiled::c_systemTxsAddress,
                    concepts::bytebuffer::toView(codeAddress)))
            {
                gasUsed = 0;
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
                executeContext.m_blockHeader.get().number(), "", transactionVersion);
            break;
        default:
            BOOST_THROW_EXCEPTION(
                std::runtime_error("Invalid receipt version: " +
                                   std::to_string(executeContext.m_transaction.get().version())));
        }

        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            TRANSACTION_EXECUTOR_LOG(TRACE)
                << "Execte transaction finished: " << receipt->toString();
        }

        co_return receipt;  // 完成第三步 Complete the third step
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<executeTransaction> /*unused*/, TransactionExecutorImpl& executor, auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, auto&& syncWait)
    {
        auto executeContext = co_await createExecuteContext(
            executor, storage, blockHeader, transaction, contextID, ledgerConfig);

        co_await transaction_executor::executeStep.operator()<0>(executeContext);
        co_await transaction_executor::executeStep.operator()<1>(executeContext);
        co_return co_await transaction_executor::executeStep.operator()<2>(executeContext);
    }
};

}  // namespace bcos::transaction_executor