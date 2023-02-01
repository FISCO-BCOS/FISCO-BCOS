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
    bcos::transaction_executor::TransactionExecutor<BackendStorage, ReceiptFactory> Executor,
    bcos::transaction_executor::StateStorage MutableStorage = storage2::memory_storage::
        MemoryStorage<transaction_executor::StateKey, transaction_executor::StateValue,
            storage2::memory_storage::Attribute(
                storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>>
class SchedulerBaseImpl
{
public:
    SchedulerBaseImpl(BackendStorage& backendStorage, ReceiptFactory& receiptFactory)
      : m_multiLayerStorage(backendStorage), m_receiptFactory(receiptFactory)
    {}

    void startExecute() { m_multiLayerStorage.newMutable(); }
    void finishExecute() { m_multiLayerStorage.pushMutableToImmutableFront(); }
    task::Task<void> commit() { co_await m_multiLayerStorage.mergeAndPopImmutableBack(); }

    task::Task<protocol::ReceiptFactoryReturnType<ReceiptFactory>> call(
        protocol::IsBlockHeader auto const& blockHeader,
        protocol::IsTransaction auto const& transaction)
    {
        auto storage = m_multiLayerStorage.fork();
        m_multiLayerStorage.newMutable();

        Executor executor(storage, m_receiptFactory);
        co_return co_await executor.execute(blockHeader, transaction, 0);
    }

private:
    MultiLayerStorage<MutableStorage, BackendStorage> m_multiLayerStorage;
    ReceiptFactory& m_receiptFactory;

protected:
    decltype(m_multiLayerStorage)& multiLayerStorage() { return m_multiLayerStorage; }
    decltype(m_receiptFactory)& receiptFactory() { return m_receiptFactory; }
};
}  // namespace bcos::transaction_scheduler