#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <tbb/parallel_pipeline.h>
#include <atomic>
#include <iterator>
#include <range/v3/view/transform.hpp>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <transaction_executor::StateStorage MultiLayerStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerParallelImpl : public SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>
{
private:
    constexpr static size_t DEFAULT_CHUNK_SIZE = 32;  // 32 transactions for one chunk

    size_t m_chunkSize = DEFAULT_CHUNK_SIZE;                  // Maybe auto adjust
    size_t m_maxToken = std::thread::hardware_concurrency();  // Maybe auto adjust
    using ChunkLocalStorage =
        transaction_scheduler::MultiLayerStorage<typename MultiLayerStorage::MutableStorage, void,
            MultiLayerStorage>;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::multiLayerStorage;

    template <class TransactionsRange>
    struct ChunkExecuteStatus
    {
        TransactionsRange transactionsRange;

        std::optional<ChunkLocalStorage> localStorage;
        std::optional<ReadWriteSetStorage<ChunkLocalStorage>> readWriteSetStorage;
        boost::container::small_vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>,
            DEFAULT_CHUNK_SIZE>
            receipts;

        std::atomic_bool finished = false;
        std::atomic_bool compareLeft = false;
        std::atomic_bool compareRight = false;

        void reset(TransactionsRange range, MultiLayerStorage& multiLayerStorage)
        {
            transactionsRange = std::move(range);
            localStorage.emplace(multiLayerStorage);
            readWriteSetStorage.emplace(*localStorage);
            receipts.clear();
            finished = false;
            compareLeft = false;
            compareRight = false;
        }

        task::Task<bool> execute(protocol::IsBlockHeader auto const& blockHeader,
            int startContextID, auto& receiptFactory, auto& tableNamePool, int64_t index,
            std::atomic_int64_t& lastChunk)
        {
            localStorage->newMutable();
            Executor<ReadWriteSetStorage<ChunkLocalStorage>,
                std::remove_cvref_t<decltype(receiptFactory)>>
                executor(*readWriteSetStorage, receiptFactory, tableNamePool);
            for (auto const& transaction : transactionsRange)
            {
                if (index >= lastChunk)
                {
                    co_return false;
                }
                receipts.emplace_back(
                    co_await executor.execute(blockHeader, transaction, startContextID++));
            }

            co_return true;
        }
    };

    static void decreaseNumber(std::atomic_int64_t& number, int64_t target)
    {
        auto current = number.load();
        while (current > target && !number.compare_exchange_strong(current, target))
        {}
    }

public:
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::SchedulerBaseImpl;
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
        std::vector<ChunkExecuteStatus<RANGES::range_value_t<decltype(chunks)>>> executedChunks(
            RANGES::size(chunks));

        auto chunkIt = RANGES::begin(executedChunks);
        while (chunkIt != RANGES::end(executedChunks))
        {
            auto offset = RANGES::distance(RANGES::begin(executedChunks), chunkIt);
            auto currentChunkView = RANGES::subrange(chunkIt, RANGES::end(executedChunks));
            auto currentChunkIt = RANGES::begin(currentChunkView);

            std::atomic_int64_t lastChunk = RANGES::size(currentChunkView);
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing...";
            tbb::parallel_pipeline(m_maxToken,
                tbb::make_filter<void, std::optional<int64_t>>(tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) {
                        int64_t index =
                            RANGES::distance(RANGES::begin(currentChunkView), currentChunkIt);
                        if (currentChunkIt == RANGES::end(currentChunkView) || index >= lastChunk)
                        {
                            control.stop();
                            return std::optional<int64_t>{};
                        }
                        if (currentChunkIt + 1 != RANGES::end(currentChunkView))
                        {
                            (currentChunkIt + 1)
                                ->reset(chunks[index + offset + 1], multiLayerStorage());
                        }
                        if (currentChunkIt == RANGES::begin(currentChunkView))
                        {
                            currentChunkIt->reset(chunks[index + offset], multiLayerStorage());
                        }
                        ++currentChunkIt;
                        return std::make_optional(index);
                    }) &
                    tbb::make_filter<std::optional<int64_t>, std::optional<int64_t>>(
                        tbb::filter_mode::parallel,
                        [&](std::optional<int64_t> input) {
                            if (input && *input < lastChunk)
                            {
                                auto index = *input;
                                auto startContextID = (offset + index) * m_chunkSize;
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Chunk " << offset + index << " executing...";
                                currentChunkView[index].finished = task::syncWait(
                                    currentChunkView[index].execute(blockHeader, startContextID,
                                        receiptFactory(), tableNamePool(), index, lastChunk));
                                if (currentChunkView[index].finished)
                                {
                                    PARALLEL_SCHEDULER_LOG(DEBUG)
                                        << "Chunk " << offset + index << " execute finished";
                                }
                                else
                                {
                                    PARALLEL_SCHEDULER_LOG(DEBUG)
                                        << "Chunk " << offset + index << " execute aborted";
                                }

                                bool expected = false;
                                // Detected RAW
                                if (index < lastChunk)
                                {
                                    if (index > 0 &&
                                        (int64_t)currentChunkView[index - 1].finished &&
                                        currentChunkView[index - 1]
                                            .compareRight.compare_exchange_strong(expected, true))
                                    {
                                        currentChunkView[index].compareLeft = true;
                                        if (currentChunkView[index - 1]
                                                .readWriteSetStorage->hasRAWIntersection(
                                                    *(currentChunkView[index].readWriteSetStorage)))
                                        {
                                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                                << "Detected RAW intersection, abort: "
                                                << offset + index;
                                            decreaseNumber(lastChunk, index);
                                        }
                                    }
                                }

                                if (index < lastChunk)
                                {
                                    expected = false;
                                    if (index < (int64_t)(RANGES::size(currentChunkView) - 1) &&
                                        currentChunkView[index + 1].finished &&
                                        currentChunkView[index]
                                            .compareRight.compare_exchange_strong(expected, true))
                                    {
                                        currentChunkView[index + 1].compareLeft = true;
                                        if (currentChunkView[index]
                                                .readWriteSetStorage->hasRAWIntersection(
                                                    *(currentChunkView[index + 1]
                                                            .readWriteSetStorage)))
                                        {
                                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                                << "Detected RAW intersection, abort: "
                                                << offset + index + 1;
                                            decreaseNumber(lastChunk, index + 1);
                                        }
                                    }
                                }

                                return std::make_optional<int64_t>(index);
                            }
                            return std::optional<int64_t>{};
                        }) &
                    tbb::make_filter<std::optional<int64_t>, void>(
                        tbb::filter_mode::serial_in_order, [&](std::optional<int64_t> input) {
                            if (!input || *input >= lastChunk)
                            {
                                return;
                            }

                            auto index = *input;
                            auto& chunkReceipts = currentChunkView[index].receipts;
                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                << "Inserting receipts... " << chunkReceipts.size();
                            RANGES::move(chunkReceipts, std::back_inserter(receipts));
                            ++chunkIt;
                        }));

            auto mergeRange = RANGES::subrange(RANGES::begin(currentChunkView), chunkIt);
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Mergeing storage... " << RANGES::size(mergeRange);
            for (auto& chunk : mergeRange)
            {
                multiLayerStorage().mutableStorage().merge(chunk.localStorage->mutableStorage());
            }
        }

        co_return receipts;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxThreads(size_t maxThreads) { m_maxToken = maxThreads; }
};
}  // namespace bcos::transaction_scheduler