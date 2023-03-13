#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <tbb/parallel_pipeline.h>
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

        bool finished = false;
        bool compareLeft = false;
        bool compareRight = false;

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

        task::Task<void> execute(protocol::IsBlockHeader auto const& blockHeader,
            int startContextID, auto& receiptFactory, auto& tableNamePool,
            std::atomic_bool& abortToken)
        {
            localStorage->newMutable();
            Executor<ReadWriteSetStorage<ChunkLocalStorage>,
                std::remove_cvref_t<decltype(receiptFactory)>>
                executor(*readWriteSetStorage, receiptFactory, tableNamePool);
            for (auto const& transaction : transactionsRange)
            {
                if (abortToken)
                {
                    break;
                }
                receipts.emplace_back(
                    co_await executor.execute(blockHeader, transaction, startContextID++));
            }

            co_return;
        }
    };

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
            std::atomic_bool abortToken = false;
            auto offset = RANGES::distance(RANGES::begin(executedChunks), chunkIt);
            auto currentChunkView = RANGES::subrange(chunkIt, RANGES::end(executedChunks));
            auto currentChunkIt = RANGES::begin(currentChunkView);

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing...";
            tbb::parallel_pipeline(m_maxToken,
                tbb::make_filter<void, std::optional<int64_t>>(tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) {
                        if (currentChunkIt == RANGES::end(currentChunkView) || abortToken)
                        {
                            control.stop();
                            return std::optional<int64_t>{};
                        }

                        int64_t index =
                            RANGES::distance(RANGES::begin(currentChunkView), currentChunkIt);
                        currentChunkView[index].reset(chunks[index + offset], multiLayerStorage());
                        ++currentChunkIt;
                        return std::make_optional(index);
                    }) &
                    tbb::make_filter<std::optional<int64_t>, std::optional<int64_t>>(
                        tbb::filter_mode::parallel,
                        [&](std::optional<int64_t> input) {
                            if (input && !abortToken)
                            {
                                auto index = *input;
                                auto startContextID = (offset + index) * m_chunkSize;
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Chunk " << offset + index << " executing...";
                                task::syncWait(currentChunkView[index].execute(blockHeader,
                                    startContextID, receiptFactory(), tableNamePool(), abortToken));
                                currentChunkView[index].finished = true;
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Chunk " << offset + index << " execute finished";

                                // Detected RAW
                                bool expected = false;
                                if (index > 0 && currentChunkView[index - 1].finished &&
                                    currentChunkView[index - 1]
                                        .compareRight.compare_exchange_strong(expected, true))
                                {
                                    currentChunkView[index].compareLeft = true;
                                    if (currentChunkView[index - 1]
                                            .readWriteSetStorage->hasRAWIntersection(
                                                *(currentChunkView[index].readWriteSetStorage)))
                                    {}
                                }

                                expected = false;
                                if (index < (RANGES::size(currentChunkView) - 1) &&
                                    currentChunkView[index + 1].finished &&
                                    currentChunkView[index].compareRight.compare_exchange_strong(
                                        expected, true))
                                {
                                    currentChunkView[index + 1].compareLeft = true;
                                    if (currentChunkView[index]
                                            .readWriteSetStorage->hasRAWIntersection(
                                                *(currentChunkView[index + 1].readWriteSetStorage)))
                                    {}
                                }

                                return std::make_optional<int64_t>(index);
                            }
                            return std::optional<int64_t>{};
                        }) &
                    tbb::make_filter<std::optional<int64_t>,
                        void>(tbb::filter_mode::serial_in_order, [&](std::optional<int64_t> input) {
                        if (!input || abortToken)
                        {
                            return;
                        }

                        auto index = *input;
                        if (index > 0)
                        {
                            if (currentChunkView[index - 1].readWriteSetStorage->hasRAWIntersection(
                                    *(currentChunkView[index].readWriteSetStorage)))
                            {
                                PARALLEL_SCHEDULER_LOG(DEBUG) << "Detected RAW intersection, abort";
                                abortToken = true;
                                return;
                            }
                        }

                        auto& chunkReceipts = currentChunkView[index].receipts;
                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Inserting receipts... " << chunkReceipts.size();
                        RANGES::move(chunkReceipts, std::back_inserter(receipts));
                        ++chunkIt;
                    }));

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Mergeing storage... " << executedChunks.size();
            for (auto& chunk : RANGES::subrange(RANGES::begin(currentChunkView), chunkIt))
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