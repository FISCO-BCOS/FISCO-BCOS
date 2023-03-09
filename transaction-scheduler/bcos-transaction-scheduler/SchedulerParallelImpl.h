#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <oneapi/tbb/task_group.h>
#include <tbb/parallel_pipeline.h>
#include <iterator>
#include <range/v3/range/concepts.hpp>
#include <range/v3/view/move.hpp>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <transaction_executor::StateStorage MultiLayerStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerParallelImpl : public SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>
{
private:
    constexpr static size_t DEFAULT_CHUNK_SIZE = 50;

    size_t m_chunkSize = DEFAULT_CHUNK_SIZE;                    // Maybe auto adjust
    size_t m_maxThreads = std::thread::hardware_concurrency();  // Maybe auto adjust
    using ChunkLocalStorage =
        transaction_scheduler::MultiLayerStorage<typename MultiLayerStorage::MutableStorage, void,
            MultiLayerStorage>;
    struct ChunkExecuteReturn
    {
        std::unique_ptr<ChunkLocalStorage> localStorage;
        ReadWriteSetStorage<ChunkLocalStorage> readWriteSetStorage;
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
    };

    task::Task<ChunkExecuteReturn> chunkExecute(protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions, int startContextID,
        std::atomic_bool& abortToken)
    {
        ChunkExecuteReturn result{
            .localStorage = std::make_unique<ChunkLocalStorage>(multiLayerStorage()),
            .readWriteSetStorage = {*(result.localStorage)},
            .receipts = {}};
        result.localStorage->newMutable();

        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            result.receipts.reserve(RANGES::size(transactions));
        }

        Executor<decltype(result.readWriteSetStorage), ReceiptFactory> executor(
            result.readWriteSetStorage, receiptFactory(), tableNamePool());
        for (auto const& transaction : transactions)
        {
            if (abortToken)
            {
                break;
            }

            result.receipts.emplace_back(
                co_await executor.execute(blockHeader, transaction, startContextID++));
        }

        co_return result;
    }

    task::Task<void> mergeStorage(auto& fromStorage, auto& toStorage)
    {
        toStorage.merge(fromStorage);
        co_return;
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

        std::vector<ChunkExecuteReturn> executedChunk;
        executedChunk.reserve(RANGES::size(chunks));
        while (chunkIt != chunkEnd)
        {
            std::atomic_bool abortToken = false;
            auto currentChunk = chunkIt;

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing...";

            tbb::parallel_pipeline(m_maxThreads,
                tbb::make_filter<void,
                    std::optional<std::tuple<RANGES::range_value_t<decltype(chunks)>, int64_t>>>(
                    tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) {
                        if (currentChunk == chunkEnd || abortToken)
                        {
                            control.stop();
                            return std::optional<
                                std::tuple<RANGES::range_value_t<decltype(chunks)>, int>>{};
                        }
                        auto chunk = currentChunk;
                        ++currentChunk;
                        return std::make_optional(
                            std::tuple<RANGES::range_value_t<decltype(chunks)>, int>{
                                *chunk, m_chunkSize * RANGES::distance(chunkBegin, chunk)});
                    }) &
                    tbb::make_filter<
                        std::optional<std::tuple<RANGES::range_value_t<decltype(chunks)>, int64_t>>,
                        std::optional<ChunkExecuteReturn>>(tbb::filter_mode::parallel,
                        [&](auto input) {
                            if (input && !abortToken)
                            {
                                auto& [transactions, startContextID] = *input;
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Chunk " << startContextID << " executing...";
                                auto result = std::make_optional(task::syncWait(chunkExecute(
                                    blockHeader, transactions, startContextID, abortToken)));
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Chunk " << startContextID << " execute finished";
                                return result;
                            }
                            return std::optional<ChunkExecuteReturn>{};
                        }) &
                    tbb::make_filter<std::optional<ChunkExecuteReturn>, void>(
                        tbb::filter_mode::serial_in_order, [&](auto chunkResult) {
                            if (!chunkResult || abortToken)
                            {
                                return;
                            }
                            auto& [chunkStorage, chunkReadWriteStorage, chunkReceipts] =
                                *chunkResult;

                            if (!executedChunk.empty())
                            {
                                auto& [_1, prevReadWriteSetStorage, _2] = executedChunk.back();
                                if (prevReadWriteSetStorage.hasRAWIntersection(
                                        chunkReadWriteStorage))
                                {
                                    // Abort the pipeline
                                    PARALLEL_SCHEDULER_LOG(DEBUG)
                                        << "Detected RAW intersection, abort";
                                    abortToken = true;
                                    return;
                                }
                            }

                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                << "Inserting receipts... " << chunkReceipts.size();
                            RANGES::move(chunkReceipts, std::back_inserter(receipts));
                            executedChunk.emplace_back(std::move(*chunkResult));
                            ++chunkIt;
                        }));
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Mergeing storage... " << executedChunk.size();
            for (auto& [executedStorage, _1, _2] : executedChunk)
            {
                co_await mergeStorage(
                    executedStorage->mutableStorage(), multiLayerStorage().mutableStorage());
            }
            executedChunk.clear();
        }

        co_return receipts;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxThreads(size_t maxThreads) { m_maxThreads = maxThreads; }
};
}  // namespace bcos::transaction_scheduler