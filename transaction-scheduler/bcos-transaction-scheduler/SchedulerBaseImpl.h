#pragma once
#include "MultiLayerStorage.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-task/Task.h>
#include <range/v3/view/transform.hpp>

namespace bcos::transaction_scheduler
{

template <transaction_executor::StateStorage MultiLayerStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerBaseImpl
{
private:
    MultiLayerStorage& m_multiLayerStorage;
    ReceiptFactory& m_receiptFactory;
    transaction_executor::TableNamePool& m_tableNamePool;

public:
    SchedulerBaseImpl(MultiLayerStorage& multiLayerStorage, ReceiptFactory& receiptFactory,
        transaction_executor::TableNamePool& tableNamePool)
      : m_multiLayerStorage(multiLayerStorage),
        m_receiptFactory(receiptFactory),
        m_tableNamePool(tableNamePool)
    {}

    void start() { m_multiLayerStorage.newMutable(); }
    task::Task<bcos::h256> finish(
        protocol::IsBlockHeader auto const& blockHeader, crypto::Hash const& hashImpl)
    {
        // State root
        auto& mutableStorage = m_multiLayerStorage.mutableStorage();
        auto it = co_await mutableStorage.seek(transaction_executor::EMPTY_STATE_KEY);
        bcos::h256 hash;
        while (co_await it.next())
        {
            bcos::h256 entryHash;
            auto tableKey = co_await it.key();
            auto& [table, key] = tableKey;
            auto tableNameView = *table;
            auto keyView = key.toStringView();
            if (co_await it.hasValue())
            {
                auto value = co_await it.value();

                entryHash = hashImpl.hash(tableNameView) ^ hashImpl.hash(keyView) ^
                            value.hash(tableNameView, keyView, hashImpl, blockHeader.version());
            }
            else
            {
                storage::Entry deleteEntry;
                deleteEntry.setStatus(storage::Entry::DELETED);
                entryHash =
                    hashImpl.hash(tableNameView) ^ hashImpl.hash(keyView) ^
                    deleteEntry.hash(tableNameView, keyView, hashImpl, blockHeader.version());
            }
            hash ^= entryHash;
        }
        m_multiLayerStorage.pushMutableToImmutableFront();

        co_return hash;
    }
    task::Task<void> commit() { co_await m_multiLayerStorage.mergeAndPopImmutableBack(); }

    task::Task<protocol::ReceiptFactoryReturnType<ReceiptFactory>> call(
        protocol::IsBlockHeader auto const& blockHeader,
        protocol::IsTransaction auto const& transaction)
    {
        auto storage = m_multiLayerStorage.fork();
        m_multiLayerStorage.newMutable();

        Executor<MultiLayerStorage, ReceiptFactory> executor(storage, m_receiptFactory);
        co_return co_await executor.execute(blockHeader, transaction, 0);
    }

    decltype(m_multiLayerStorage)& multiLayerStorage() & { return m_multiLayerStorage; }
    decltype(m_receiptFactory)& receiptFactory() & { return m_receiptFactory; }
    transaction_executor::TableNamePool& tableNamePool() & { return m_tableNamePool; }
};

}  // namespace bcos::transaction_scheduler