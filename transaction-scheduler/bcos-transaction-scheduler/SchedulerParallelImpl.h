#pragma once

#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include <bcos-task/Wait.h>
#include <tbb/pipeline.h>
#include <iterator>

namespace bcos::transaction_scheduler
{
template <transaction_executor::StateStorage MultiLayerStorage,
    protocol::IsTransactionReceiptFactory ReceiptFactory,
    template <typename, typename> class Executor>
class SchedulerParallelImpl : public SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>
{
private:
    constexpr static size_t DEFAULT_CHUNK_SIZE = 1000;
    constexpr static size_t DEFAULT_MAX_THREADS = 16;

    size_t m_chunkSize = DEFAULT_CHUNK_SIZE;    // Maybe auto modify
    size_t m_maxThreads = DEFAULT_MAX_THREADS;  // Maybe auto modify

    using ChunkStorage = std::tuple<ReadWriteSetStorage<MultiLayerStorage>, MultiLayerStorage>;
    using ChunkExecuteReturn =
        std::tuple<ChunkStorage, std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>>;

    ChunkExecuteReturn chunkExecute(protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto&& transactions, int startContextID)
    {
        auto myStorage = multiLayerStorage().fork();
        myStorage.newMutable();
        ReadWriteSetStorage<MultiLayerStorage> readWriteSetStorage(myStorage);

        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> chunkReceipts;
        chunkReceipts.reserve(RANGES::size(transactions));
        task::syncWait([&readWriteSetStorage](decltype(chunkReceipts)& chunkReceipts,
                           decltype(transactions)& transactions, decltype(blockHeader)& blockHeader,
                           int contextID) -> task::Task<void> {
            Executor<decltype(readWriteSetStorage), ReceiptFactory> executor(
                readWriteSetStorage, receiptFactory(), tableNamePool());
            for (auto const& transaction : transactions)
            {
                chunkReceipts.emplace_back(
                    co_await executor.execute(blockHeader, transaction, contextID++));
            }
        }(chunkReceipts, transactions, blockHeader, startContextID));

        return {ChunkStorage{std::move(readWriteSetStorage), std::move(myStorage)},
            std::move(chunkReceipts)};  // Notice the m_storage of readWriteSetStorage will be
                                        // invalid
    }

    void mergeStorage(auto const& fromStorage, auto& toStorage)
    {
        auto it = co_await fromStorage->seek(transaction_executor::EMPTY_STATE_KEY);
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
        }
    }

public:
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::SchedulerBaseImpl;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::multiLayerStorage;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::receiptFactory;
    using SchedulerBaseImpl<MultiLayerStorage, ReceiptFactory, Executor>::tableNamePool;

    task::Task<std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)  // protocol::Transaction
    {
        std::vector<protocol::ReceiptFactoryReturnType<ReceiptFactory>> receipts;
        if constexpr (RANGES::sized_range<decltype(transactions)>)
        {
            receipts.reserve(RANGES::size(transactions));
        }

        int contextID = 0;
        auto chunks = transactions | RANGES::views::chunk(m_chunkSize);
        auto chunkIt = RANGES::begin(chunks);
        auto chunkEnd = RANGES::end(chunks);

        multiLayerStorage().newMutable();

        std::vector<ChunkExecuteReturn> executedStorages;
        while (chunkIt != chunkEnd)
        {
            std::atomic_bool aborted = false;
            auto currentChunk = chunkIt;
            tbb::parallel_pipeline(m_maxThreads,
                tbb::make_filter<void, RANGES::range_value_t<decltype(chunks)>>(
                    tbb::filter::serial_in_order,
                    [&](tbb::flow_control& control) -> RANGES::range_value_t<decltype(chunks)> {
                        if (currentChunk == chunkEnd || aborted)
                        {
                            control.stop();
                            return {};
                        }
                        return {*(currentChunk++), 0};
                    }) &
                    tbb::make_filter<std::tuple<RANGES::range_value_t<decltype(chunks)>, int>,
                        ChunkExecuteReturn>(tbb::filter::parallel,
                        [&](RANGES::range_value_t<decltype(chunks)> const& input) {
                            auto&& [transactions, startContextID] = input;
                            return chunkExecute(blockHeader, transactions, startContextID);
                        }) &
                    tbb::make_filter<ChunkExecuteReturn, void>(
                        tbb::filter::serial_in_order, [&](auto&& chunkResult) {
                            if (!aborted)
                            {
                                auto&& [chunkStorage, chunkReceipts] = chunkResult;
                                auto&& [readWriteSetStorage, myStorage] = chunkStorage;

                                if (!executedStorages.empty())
                                {
                                    auto&& [prevReadWriteSetStorage, oldStorage] =
                                        executedStorages.back();
                                    if (prevReadWriteSetStorage.hasRAWIntersection(
                                            readWriteSetStorage))
                                    {
                                        // Abort the pipeline and retry
                                        aborted = true;
                                        return;
                                    }
                                }

                                executedStorages.emplace_back(std::move(chunkStorage));
                                receipts.insert(receipts.end(),
                                    std::make_move_iterator(chunkReceipts.begin()),
                                    std::make_move_iterator(chunkResult.end()));

                                mergeStorage(myStorage.mutableStorage(),
                                    multiLayerStorage().mutableStorage());

                                RANGES::advance(chunkIt, 1);
                            }
                        }));
        }

        co_return receipts;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxThreads(size_t maxThreads) { m_maxThreads = maxThreads; }
};
}  // namespace bcos::transaction_scheduler