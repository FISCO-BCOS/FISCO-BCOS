#pragma once

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-transaction-executor/vm/VMFactory.h"
#include "transaction-executor/bcos-transaction-executor/RollbackableStorage.h"
#include "transaction-executor/bcos-transaction-executor/vm/VMInstance.h"
#include "vm/HostContext.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <gsl/util>
#include <iterator>
#include <type_traits>

namespace bcos::transaction_executor
{

#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

// clang-format off
struct InvalidArgumentsError: public bcos::Error {};
// clang-format on

template <StateStorage Storage>
class TransactionExecutorImpl
{
private:
    evmc_address unhexAddress(std::string_view view)
    {
        if (view.empty())
        {
            return {};
        }
        if (view.starts_with("0x"))
        {
            view = view.substr(2);
        }

        evmc_address address;
        if (view.empty())
        {
            std::uninitialized_fill(address.bytes, address.bytes + sizeof(address.bytes), 0);
        }
        else
        {
            boost::algorithm::unhex(view, address.bytes);
        }
        return address;
    }

    VMFactory vmFactory;
    Storage& m_storage;
    protocol::TransactionReceiptFactory& m_receiptFactory;
    TableNamePool& m_tableNamePool;

public:
    TransactionExecutorImpl(Storage& storage, protocol::TransactionReceiptFactory& receiptFactory,
        TableNamePool& tableNamePool)
      : m_storage(storage), m_receiptFactory(receiptFactory), m_tableNamePool(tableNamePool)
    {}

    task::Task<protocol::TransactionReceipt::Ptr> execute(
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
                .gas = 30000 * 10000,  // TODO: use arg
                .recipient = toAddress,
                .destination_ptr = nullptr,
                .destination_len = 0,
                .sender = {},
                .sender_ptr = nullptr,
                .sender_len = 0,
                .input_data = transaction.input().data(),
                .input_size = transaction.input().size(),
                .value = {},
                .create2_salt = {},
                .code_address = toAddress};
            std::uninitialized_copy(
                transaction.sender().begin(), transaction.sender().end(), evmcMessage.sender.bytes);

            int64_t seq = 0;
            HostContext hostContext(vmFactory, rollbackableStorage, m_tableNamePool, blockHeader,
                evmcMessage, evmcMessage.sender, contextID, seq);
            auto evmcResult = co_await hostContext.execute();
            auto finallyAction = gsl::finally([&]() { releaseResult(evmcResult); });

            bcos::bytesConstRef output;
            std::string newContractAddress;
            if (!RANGES::equal(evmcResult.create_address.bytes, EMPTY_ADDRESS.bytes))
            {
                newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
                boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                    evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                    std::back_inserter(newContractAddress));
            }
            else
            {
                output = {evmcResult.output_data, evmcResult.output_size};
            }

            if (evmcResult.status_code != 0)
            {
                TRANSACTION_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;
            }

            auto const& logEntries = hostContext.logs();
            auto receipt =
                m_receiptFactory.createReceipt(evmcResult.gas_left, std::move(newContractAddress),
                    logEntries, evmcResult.status_code, output, blockHeader.number());

            co_return receipt;
        }
        catch (std::exception& e)
        {
            TRANSACTION_EXECUTOR_LOG(DEBUG)
                << "Execute exception: " << boost::diagnostic_information(e);

            auto receipt = m_receiptFactory.createReceipt(
                0, {}, {}, EVMC_INTERNAL_ERROR, {}, blockHeader.number());
            receipt->setMessage(boost::diagnostic_information(e));
            co_return receipt;
        }
    }
};

}  // namespace bcos::transaction_executor