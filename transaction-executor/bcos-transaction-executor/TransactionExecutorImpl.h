#pragma once

#include "bcos-table/src/StateStorage.h"
#include "transaction-executor/bcos-transaction-executor/RollbackableStorage.h"
#include "transaction-executor/bcos-transaction-executor/vm/VMInstance.h"
#include "vm/HostContext.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/type_traits/aligned_storage.hpp>
#include <iterator>
#include <type_traits>

namespace bcos::transaction_executor
{

template <StateStorage Storage, protocol::IsTransactionReceiptFactory ReceiptFactory>
class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(Storage& storage, ReceiptFactory& receiptFactory)
      : m_storage(storage), m_receiptFactory(receiptFactory)
    {}

    task::Task<protocol::ReceiptFactoryReturnType<ReceiptFactory>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        protocol::IsTransaction auto const& transaction, int contextID)
    {
        constexpr static evmc_address EMPTY_ADDRESS = {};

        try
        {
            Rollbackable<std::remove_reference_t<decltype(m_storage)>> rollbackableStorage(
                m_storage);

            auto toAddress = unhexAddress(transaction.to());
            evmc_message evmcMessage = {.kind = transaction.to().empty() ? EVMC_CREATE : EVMC_CALL,
                .flags = 0,
                .depth = 0,
                .gas = 3000 * 10000,  // TODO: use arg
                .recipient = toAddress,
                .destination_ptr = nullptr,
                .destination_len = 0,
                .sender = unhexAddress(transaction.sender()),
                .sender_ptr = nullptr,
                .sender_len = 0,
                .input_data = transaction.input().data(),
                .input_size = transaction.input().size(),
                .value = {},
                .create2_salt = {},
                .code_address = toAddress};

            HostContext hostContext(rollbackableStorage, m_tableNamePool, blockHeader, evmcMessage,
                evmcMessage.sender, contextID, 0);
            auto evmcResult = co_await hostContext.execute();

            std::string newContractAddress;
            if (!RANGES::equal(evmcResult.create_address.bytes, EMPTY_ADDRESS.bytes))
            {
                newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
                boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                    evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                    std::back_inserter(newContractAddress));
            }

            auto const& logEntries = hostContext.logs();
            auto receipt = m_receiptFactory.createReceipt(evmcResult.gas_left,
                std::move(newContractAddress), logEntries, evmcResult.status_code,
                bcos::bytesConstRef(evmcResult.output_data, evmcResult.output_size),
                blockHeader.number());

            releaseResult(evmcResult);
            co_return receipt;
        }
        catch (std::exception& e)
        {
            auto receipt = m_receiptFactory.createReceipt(
                0, {}, {}, EVMC_INTERNAL_ERROR, {}, blockHeader.number());
            co_return receipt;
        }
    }

private:
    evmc_address unhexAddress(std::string_view view)
    {
        if (view.empty())
        {
            return {};
        }

        evmc_address address;
        boost::algorithm::unhex(view, address.bytes);
        return address;
    }

    Storage& m_storage;
    ReceiptFactory& m_receiptFactory;
    TableNamePool m_tableNamePool;
};

}  // namespace bcos::transaction_executor