#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>

namespace bcos::transaction_executor
{

template <class StorageType>
concept ExecutorStorage =
    storage2::Storage<StorageType, std::tuple<std::string, std::string>, storage::Entry>;

template <ExecutorStorage Storage, class ReceiptFactory>
class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(
        protocol::BlockHeader const& blockHeader, Storage& storage, ReceiptFactory& receiptFactory)
      : m_blockHeader(blockHeader), m_storage(storage), m_receiptFactory(receiptFactory)
    {}

    task::Task<std::unique_ptr<protocol::TransactionReceipt>> execute(
        const protocol::Transaction& transaction)
    {
        
    }

private:
    protocol::BlockHeader const& m_blockHeader;
    Storage& m_storage;
    ReceiptFactory& m_receiptFactory;
};

}  // namespace bcos::transaction_executor