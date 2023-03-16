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
#include <oneapi/tbb/combinable.h>
#include <oneapi/tbb/parallel_pipeline.h>

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
        auto& mutableStorage = m_multiLayerStorage.mutableStorage();
        auto it = co_await mutableStorage.seek(storage2::STORAGE_BEGIN);

        auto range = it.range();
        tbb::combinable<bcos::h256> combinableHash;

        auto currentRangeIt = RANGES::begin(range);
        tbb::parallel_pipeline(std::thread::hardware_concurrency(),
            tbb::make_filter<void,
                std::optional<decltype(RANGES::subrange<decltype(RANGES::begin(range))>(
                    RANGES::begin(range), RANGES::end(range)))>>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control)
                    -> std::optional<decltype(RANGES::subrange<decltype(RANGES::begin(range))>(
                        RANGES::begin(range), RANGES::end(range)))> {
                    if (currentRangeIt == RANGES::end(range))
                    {
                        control.stop();
                        return {};
                    }

                    auto start = currentRangeIt;
                    constexpr static int HASH_CHUNK_SIZE = 100;
                    for (auto num = 0;
                         num < HASH_CHUNK_SIZE && currentRangeIt != RANGES::end(range); ++num)
                    {
                        ++currentRangeIt;
                    }
                    return std::make_optional(
                        RANGES::subrange<decltype(start)>(start, currentRangeIt));
                }) &
                tbb::make_filter<std::optional<decltype(RANGES::subrange<decltype(RANGES::begin(
                                         range))>(RANGES::begin(range), RANGES::end(range)))>,
                    void>(tbb::filter_mode::parallel,
                    [&combinableHash, &hashImpl, &blockHeader](auto const& entryRange) {
                        if (!entryRange)
                        {
                            return;
                        }

                        auto& entryHash = combinableHash.local();
                        for (auto const& keyValuePair : *entryRange)
                        {
                            auto& [key, entry] = keyValuePair;
                            auto& [tableName, keyName] = *key;
                            auto tableNameView = *tableName;
                            auto keyView = keyName.toStringView();
                            if (entry)
                            {
                                entryHash ^= entry->hash(
                                    tableNameView, keyView, hashImpl, blockHeader.version());
                            }
                            else
                            {
                                storage::Entry deleteEntry;
                                deleteEntry.setStatus(storage::Entry::DELETED);
                                entryHash ^= deleteEntry.hash(
                                    tableNameView, keyView, hashImpl, blockHeader.version());
                            }
                        }
                    }));
        m_multiLayerStorage.pushMutableToImmutableFront();

        co_return combinableHash.combine(
            [](const bcos::h256& lhs, const bcos::h256& rhs) -> bcos::h256 { return lhs ^ rhs; });
    }
    task::Task<void> commit() { co_await m_multiLayerStorage.mergeAndPopImmutableBack(); }

    task::Task<protocol::ReceiptFactoryReturnType<ReceiptFactory>> call(
        protocol::IsBlockHeader auto const& blockHeader,
        protocol::IsTransaction auto const& transaction)
    {
        auto storage = m_multiLayerStorage.fork(false);
        storage->newMutable();

        Executor<MultiLayerStorage, ReceiptFactory> executor(
            *storage, m_receiptFactory, m_tableNamePool);
        co_return co_await executor.execute(blockHeader, transaction, 0);
    }

    MultiLayerStorage& multiLayerStorage() & { return m_multiLayerStorage; }
    decltype(m_receiptFactory)& receiptFactory() & { return m_receiptFactory; }
    transaction_executor::TableNamePool& tableNamePool() & { return m_tableNamePool; }
};

}  // namespace bcos::transaction_scheduler