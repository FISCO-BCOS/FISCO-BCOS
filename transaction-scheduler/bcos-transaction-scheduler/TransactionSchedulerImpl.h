#pragma once
#include "MultiLayerStorage.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-task/Task.h>

namespace bcos::transaction_scheduler
{

template <bcos::transaction_executor::StateStorage BackendStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    bcos::transaction_executor::StateStorage MutableStorage = storage2::memory_storage::
        MemoryStorage<transaction_executor::StateKey, transaction_executor::StateValue,
            storage2::memory_storage::Attribute(
                storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>>
class TransactionSchedulerImpl
{
public:
    TransactionSchedulerImpl(BackendStorage& backendStorage) : m_multiLayerStorage(backendStorage)
    {}

    void nextBlock() { m_multiLayerStorage.newMutable(); }

    task::Task<std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>> executeTransactions(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }


    }

private:
    MultiLayerStorage<MutableStorage, BackendStorage> m_multiLayerStorage;
};
}  // namespace bcos::transaction_scheduler