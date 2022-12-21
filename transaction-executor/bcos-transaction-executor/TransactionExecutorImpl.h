#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>

namespace bcos::transaction_executor
{

template <storage2::Storage Storage>
class TransactionExecutorImpl : public TransactionExecutorBase<TransactionExecutorImpl<Storage>>
{
public:
    TransactionExecutorImpl(protocol::BlockHeader const& blockHeader, Storage& storage)
      : m_blockHeader(blockHeader), m_storage(storage)
    {}

    task::Task<std::unique_ptr<protocol::TransactionReceipt>> impl_execute(
        const protocol::Transaction& transaction)
    {}

private:
    protocol::BlockHeader const& m_blockHeader;
    Storage& m_storage;
};

}  // namespace bcos::transaction_executor