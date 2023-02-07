#pragma once
#include "MultiLayerStorage.h"
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

template <bcos::transaction_executor::StateStorage BackendStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    bcos::transaction_executor::TransactionExecutor<BackendStorage, ReceiptFactory> Executor,
    bcos::transaction_executor::StateStorage MutableStorage>
class SchedulerBaseImpl
{
public:
    SchedulerBaseImpl(BackendStorage& backendStorage, ReceiptFactory& receiptFactory)
      : m_multiLayerStorage(std::forward<BackendStorage>(backendStorage)),
        m_receiptFactory(receiptFactory)
    {}

    void start() { m_multiLayerStorage.newMutable(); }
    task::Task<bcos::h256> finish(
        protocol::IsBlockHeader auto const& blockHeader, crypto::Hash const& hashImpl)
    {
        // State root
        auto mutableStorage = m_multiLayerStorage.top();
        auto it = co_await mutableStorage.seek(transaction_executor::EMPTY_STATE_KEY);
        bcos::h256 hash;
        while (co_await it.next())
        {
            bcos::h256 entryHash;
            auto tableKey = co_await it.key();
            auto& [table, key] = tableKey;
            auto tableName = *table;
            auto keyView = key.toStringView();
            if (co_await it.hasValue())
            {
                auto value = co_await it.value();

                entryHash = hashImpl.hash(tableName) ^ hashImpl.hash(keyView) ^
                            value.hash(tableName, keyView, hashImpl, blockHeader.version());
            }
            else
            {
                storage::Entry deleteEntry;
                deleteEntry.setStatus(storage::Entry::DELETED);
                entryHash = hashImpl.hash(tableName) ^ hashImpl.hash(keyView) ^
                            deleteEntry.hash(tableName, keyView, hashImpl, blockHeader.version());
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

// template <class Scheduler>
// inline task::Task<> executeBlock(Scheduler& scheduler, protocol::IsBlock auto const& block)
// {
//     auto blockHeaderPtr = block.blockHeaderConst();

//     scheduler.start();

//     auto transactions =
//         RANGES::iota_view<uint64_t, uint64_t>(0LU, block.transactionSize()) |
//         RANGES::views::transform([&block](uint64_t index) { return block.transaction(index); }) |
//         RANGES::to<std::vector<protocol::Transaction::ConstPtr>>();

//     scheduler.execute(*blockHeaderPtr,
//         transactions |
//             RANGES::views::transform([](protocol::Transaction::ConstPtr const& transactionPtr) {
//                 return *transactionPtr;
//             }));

//     scheduler.finish();
// }
}  // namespace bcos::transaction_scheduler