#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <tbb/parallel_pipeline.h>
#include <iterator>
#include <range/v3/range/concepts.hpp>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <transaction_executor::StateStorage MultiLayerStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerParallelImpl : public SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>
{
private:
    constexpr static size_t DEFAULT_CHUNK_SIZE = 100;
    constexpr static size_t DEFAULT_MAX_THREADS = 16;  // Use hardware concurrency, window

    size_t m_chunkSize = DEFAULT_CHUNK_SIZE;    // Maybe auto adjust
    size_t m_maxThreads = DEFAULT_MAX_THREADS;  // Maybe auto adjust
    using ChunkExecuteStorage =
        transaction_scheduler::MultiLayerStorage<typename MultiLayerStorage::MutableStorage, void,
            MultiLayerStorage>;
    struct ChunkStorage
    {
        ReadWriteSetStorage<ChunkExecuteStorage> readWriteSetStorage;
        std::unique_ptr<ChunkExecuteStorage> multiLayerStorage;
    };
    using ChunkExecuteReturn =
        std::tuple<ChunkStorage, std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>>;

    task::Task<ChunkExecuteReturn> chunkExecute(protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions, int startContextID,
        std::atomic_bool& abortToken)
    {
        auto myStorage = std::make_unique<ChunkExecuteStorage>(multiLayerStorage());
        myStorage->newMutable();
        ReadWriteSetStorage<ChunkExecuteStorage> readWriteSetStorage(*myStorage);

        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> chunkReceipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            chunkReceipts.reserve(RANGES::size(transactions));
        }

        Executor<decltype(readWriteSetStorage), ReceiptFactory> executor(
            readWriteSetStorage, receiptFactory(), tableNamePool());
        for (auto const& transaction : transactions)
        {
            if (abortToken)
            {
                break;
            }

            chunkReceipts.emplace_back(
                co_await executor.execute(blockHeader, transaction, startContextID++));
        }

        auto chunkStorage = ChunkStorage{.readWriteSetStorage = std::move(readWriteSetStorage),
            .multiLayerStorage = std::move(myStorage)};
        co_return ChunkExecuteReturn{std::move(chunkStorage), std::move(chunkReceipts)};
    }

    task::Task<void> mergeStorage(auto& fromStorage, auto& toStorage)
    {
        auto it = co_await fromStorage.seek(storage2::STORAGE_BEGIN);
        size_t count = 0;
        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                co_await storage2::writeOne(toStorage, co_await it.key(), co_await it.value());
            }
            else
            {
                co_await storage2::removeOne(toStorage, co_await it.key());
            }
            ++count;
        }

        PARALLEL_SCHEDULER_LOG(DEBUG) << "Merged " << count << " entries";
    }

public:
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::SchedulerBaseImpl;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::multiLayerStorage;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::receiptFactory;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::tableNamePool;

    task::Task<std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        auto chunks = transactions | RANGES::views::chunk(m_chunkSize);
        auto chunkIt = RANGES::begin(chunks);

        auto chunkBegin = chunkIt;
        auto chunkEnd = RANGES::end(chunks);

        std::vector<ChunkStorage> executedStorages;
        executedStorages.reserve(RANGES::size(chunks));
        while (chunkIt != chunkEnd)
        {
            std::atomic_bool abortToken = false;
            auto currentChunk = chunkIt;
            tbb::parallel_pipeline(m_maxThreads,
                tbb::make_filter<void,
                    std::optional<std::tuple<RANGES::range_value_t<decltype(chunks)>, int>>>(
                    tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) {
                        if (currentChunk == chunkEnd || abortToken)
                        {
                            control.stop();
                            return std::optional<
                                std::tuple<RANGES::range_value_t<decltype(chunks)>, int>>{};
                        }
                        auto chunk = currentChunk;
                        RANGES::advance(currentChunk, 1);
                        return std::make_optional(
                            std::tuple<RANGES::range_value_t<decltype(chunks)>, int>{
                                *chunk, m_chunkSize * RANGES::distance(chunkBegin, chunk)});
                    }) &
                    tbb::make_filter<
                        std::optional<std::tuple<RANGES::range_value_t<decltype(chunks)>, int>>,
                        std::optional<ChunkExecuteReturn>>(tbb::filter_mode::parallel,
                        [&](auto&& input) {
                            if (input && !abortToken)
                            {
                                PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk executing...";
                                auto&& [transactions, startContextID] = *input;
                                PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk execute finished";
                                return std::make_optional(task::syncWait(chunkExecute(
                                    blockHeader, transactions, startContextID, abortToken)));
                            }
                            return std::optional<ChunkExecuteReturn>{};
                        }) &
                    tbb::make_filter<std::optional<ChunkExecuteReturn>, void>(
                        tbb::filter_mode::serial_in_order, [&](auto&& chunkResult) {
                            if (chunkResult && !abortToken)
                            {
                                auto&& [chunkStorage, chunkReceipts] = *chunkResult;
                                auto&& [readWriteSetStorage, _] = chunkStorage;

                                if (!executedStorages.empty())
                                {
                                    auto& [prevReadWriteSetStorage, oldStorage] =
                                        executedStorages.back();
                                    if (prevReadWriteSetStorage.hasRAWIntersection(
                                            readWriteSetStorage))
                                    {
                                        // Abort the pipeline
                                        PARALLEL_SCHEDULER_LOG(DEBUG)
                                            << "Detected raw intersection, abort";
                                        abortToken = true;
                                        return;
                                    }
                                }

                                executedStorages.emplace_back(std::move(chunkStorage));
                                receipts.insert(receipts.end(),
                                    std::make_move_iterator(chunkReceipts.begin()),
                                    std::make_move_iterator(chunkReceipts.end()));

                                RANGES::advance(chunkIt, 1);
                            }
                        }));
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Mergeing storage...";
            for (auto& [_, executedStorage] : executedStorages)
            {
                co_await mergeStorage(
                    executedStorage->mutableStorage(), multiLayerStorage().mutableStorage());
            }
            executedStorages.clear();
        }

        co_return receipts;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxThreads(size_t maxThreads) { m_maxThreads = maxThreads; }
};
}  // namespace bcos::transaction_scheduler