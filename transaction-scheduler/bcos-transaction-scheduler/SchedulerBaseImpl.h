#pragma once
#include "MultiLayerStorage.h"
#include "bcos-framework/protocol/Transaction.h"
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-task/Task.h>
#include <oneapi/tbb/combinable.h>
#include <oneapi/tbb/parallel_pipeline.h>

namespace bcos::transaction_scheduler
{

template <class MultiLayerStorage, template <typename> class Executor>
class SchedulerBaseImpl
{
private:
    MultiLayerStorage& m_multiLayerStorage;
    protocol::TransactionReceiptFactory& m_receiptFactory;
    transaction_executor::TableNamePool& m_tableNamePool;

public:
    SchedulerBaseImpl(MultiLayerStorage& multiLayerStorage,
        protocol::TransactionReceiptFactory& receiptFactory,
        transaction_executor::TableNamePool& tableNamePool)
      : m_multiLayerStorage(multiLayerStorage),
        m_receiptFactory(receiptFactory),
        m_tableNamePool(tableNamePool)
    {}

    void start() { m_multiLayerStorage.newMutable(); }
    task::Task<bcos::h256> finish(
        protocol::IsBlockHeader auto const& blockHeader, crypto::Hash const& hashImpl)
    {
        auto& mutableStorage = m_multiLayerStorage.mutableStorage();
        auto it = co_await mutableStorage.seek(storage2::STORAGE_BEGIN);

        static constexpr int HASH_CHUNK_SIZE = 32;
        auto range = it.range() | RANGES::views::chunk(HASH_CHUNK_SIZE);
        tbb::combinable<bcos::h256> combinableHash;

        tbb::task_group taskGroup;
        for (auto&& subrange : range)
        {
            taskGroup.run([subrange = std::forward<decltype(subrange)>(subrange), &combinableHash,
                              &blockHeader, &hashImpl]() {
                auto& entryHash = combinableHash.local();

                for (auto const& keyValue : subrange)
                {
                    auto& [key, entry] = keyValue;
                    auto& [tableName, keyName] = *key;
                    auto tableNameView = *tableName;
                    auto keyView = keyName.toStringView();
                    if (entry)
                    {
                        entryHash ^=
                            entry->hash(tableNameView, keyView, hashImpl, blockHeader.version());
                    }
                    else
                    {
                        storage::Entry deleteEntry;
                        deleteEntry.setStatus(storage::Entry::DELETED);
                        entryHash ^= deleteEntry.hash(
                            tableNameView, keyView, hashImpl, blockHeader.version());
                    }
                }
            });
        }
        taskGroup.wait();
        m_multiLayerStorage.pushMutableToImmutableFront();

        co_return combinableHash.combine(
            [](const bcos::h256& lhs, const bcos::h256& rhs) -> bcos::h256 { return lhs ^ rhs; });
    }
    task::Task<void> commit() { co_await m_multiLayerStorage.mergeAndPopImmutableBack(); }

    task::Task<protocol::TransactionReceipt::Ptr> call(
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction)
    {
        auto view = m_multiLayerStorage.fork(false);
        view.newTemporaryMutable();

        Executor<decltype(view)> executor(view, m_receiptFactory, m_tableNamePool);
        co_return co_await executor.execute(blockHeader, transaction, 0);
    }

    MultiLayerStorage& multiLayerStorage() & { return m_multiLayerStorage; }
    decltype(m_receiptFactory)& receiptFactory() & { return m_receiptFactory; }
    transaction_executor::TableNamePool& tableNamePool() & { return m_tableNamePool; }
};

}  // namespace bcos::transaction_scheduler