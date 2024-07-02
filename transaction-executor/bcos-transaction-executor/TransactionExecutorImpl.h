#pragma once

#include "RollbackableStorage.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Generator.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "precompiled/PrecompiledManager.h"
#include "vm/HostContext.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <gsl/util>
#include <iterator>
#include <type_traits>

namespace bcos::transaction_executor
{
#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(
        protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl);

private:
    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    PrecompiledManager m_precompiledManager;

    static evmc_message newEVMCMessage(protocol::Transaction const& transaction, int64_t gasLimit);

    friend task::Generator<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<execute3Step> /*unused*/, TransactionExecutorImpl& executor, auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, auto&& waitOperator)
    {
        protocol::TransactionReceipt::Ptr receipt;
        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            TRANSACTION_EXECUTOR_LOG(TRACE) << "Execute transaction: " << toHex(transaction.hash());
        }

        Rollbackable<std::decay_t<decltype(storage)>> rollbackableStorage(storage);
        // create a transient storage
        using MemoryStorageType =
            bcos::storage2::memory_storage::MemoryStorage<bcos::transaction_executor::StateKey,
                bcos::transaction_executor::StateValue,
                bcos::storage2::memory_storage::Attribute(
                    bcos::storage2::memory_storage::ORDERED |
                    bcos::storage2::memory_storage::LOGICAL_DELETION)>;
        MemoryStorageType transientStorage;
        Rollbackable<MemoryStorageType> rollbackableTransientStorage(transientStorage);
        auto gasLimit = static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()));
        auto evmcMessage = newEVMCMessage(transaction, gasLimit);

        if (blockHeader.number() == 0 && transaction.to() == precompiled::AUTH_COMMITTEE_ADDRESS)
        {
            evmcMessage.kind = EVMC_CREATE;
        }

        int64_t seq = 0;
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, evmcMessage,
                evmcMessage.sender, transaction.abi(), contextID, seq,
                executor.m_precompiledManager, ledgerConfig, *executor.m_hashImpl,
                std::forward<decltype(waitOperator)>(waitOperator));

        waitOperator(hostContext.prepare());
        co_yield receipt;  // 完成第一步 Complete the first step

        auto evmcResult = waitOperator(hostContext.execute());
        co_yield receipt;  // 完成第二步 Complete the second step

        std::string newContractAddress;
        if (evmcMessage.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
        {
            newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
            boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                std::back_inserter(newContractAddress));
        }
        auto output = bcos::bytesConstRef{evmcResult.output_data, evmcResult.output_size};

        if (evmcResult.status_code != 0)
        {
            TRANSACTION_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;
        }

        int32_t receiptStatus =
            evmcResult.status_code == EVMC_REVERT ?
                static_cast<int32_t>(protocol::TransactionStatus::RevertInstruction) :
                evmcResult.status_code;
        auto const& logEntries = hostContext.logs();
        auto transactionVersion =
            static_cast<bcos::protocol::TransactionVersion>(transaction.version());
        switch (transactionVersion)
        {
        case bcos::protocol::TransactionVersion::V0_VERSION:
            receipt = executor.m_receiptFactory.get().createReceipt(gasLimit - evmcResult.gas_left,
                newContractAddress, logEntries, receiptStatus, output, blockHeader.number());
            break;
        case bcos::protocol::TransactionVersion::V1_VERSION:
        case bcos::protocol::TransactionVersion::V2_VERSION:
            receipt = executor.m_receiptFactory.get().createReceipt2(gasLimit - evmcResult.gas_left,
                newContractAddress, logEntries, receiptStatus, output, blockHeader.number(), "",
                transactionVersion);
            break;
        default:
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "Invalid receipt version: " + std::to_string(transaction.version())));
        }

        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            TRANSACTION_EXECUTOR_LOG(TRACE)
                << "Execte transaction finished" << ", gasUsed: " << receipt->gasUsed()
                << ", newContractAddress: " << receipt->contractAddress()
                << ", logEntries: " << receipt->logEntries().size()
                << ", status: " << receipt->status() << ", output: " << toHex(receipt->output())
                << ", blockNumber: " << receipt->blockNumber() << ", receipt: " << receipt->hash()
                << ", version: " << receipt->version();
        }

        co_yield receipt;  // 完成第三步 Complete the third step
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        tag_t<executeTransaction> /*unused*/, TransactionExecutorImpl& executor, auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, auto&& waitOperator)
    {
        for (auto receipt : execute3Step(executor, storage, blockHeader, transaction, contextID,
                 ledgerConfig, std::forward<decltype(waitOperator)>(waitOperator)))
        {
            if (receipt)
            {
                co_return receipt;
            }
        }
        co_return protocol::TransactionReceipt::Ptr{};
    }
};

}  // namespace bcos::transaction_executor