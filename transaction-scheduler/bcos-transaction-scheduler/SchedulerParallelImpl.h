#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-task/Wait.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <iterator>
#include <stdexcept>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <class MultiLayerStorage, template <typename> class Executor>
class SchedulerParallelImpl : public SchedulerBaseImpl<MultiLayerStorage, Executor>
{
private:
    constexpr static size_t DEFAULT_CHUNK_SIZE = 32;          // 32 transactions for one chunk
    size_t m_chunkSize = DEFAULT_CHUNK_SIZE;                  // Maybe auto adjust
    size_t m_maxToken = std::thread::hardware_concurrency();  // Maybe auto adjust
    std::unique_ptr<tbb::task_group> m_asyncTaskGroup;
    using ChunkLocalStorage =
        transaction_scheduler::MultiLayerStorage<typename MultiLayerStorage::MutableStorage, void,
            decltype(std::declval<MultiLayerStorage>().fork(true))>;
    using SchedulerBaseImpl<MultiLayerStorage, Executor>::multiLayerStorage;

    template <class TransactionAndReceiptssRange>
    class ChunkExecuteStatus
    {
    private:
        struct Storage
        {
            Storage(auto& storage)
              : m_localStorage(storage),
                m_localStorageView(forkAndMutable(m_localStorage)),
                m_readWriteSetStorage(m_localStorageView)
            {}

            ChunkLocalStorage m_localStorage;
            decltype(std::declval<ChunkLocalStorage>().fork(true)) m_localStorageView;
            ReadWriteSetStorage<decltype(m_localStorageView)> m_readWriteSetStorage;

            auto forkAndMutable(auto& storage)
            {
                storage.newMutable();
                auto view = storage.fork(true);
                return view;
            }
        };

        int64_t m_chunkIndex = 0;
        std::atomic_int64_t* m_lastChunkIndex = nullptr;
        std::optional<Storage> m_storages;
        TransactionAndReceiptssRange m_transactionAndReceiptsRange;

        std::atomic_bool m_finished = false;
        std::atomic_bool m_comparePrev = false;
        std::atomic_bool m_compareNext = false;

    public:
        void init(int64_t chunkIndex, std::atomic_int64_t& lastChunkIndex,
            TransactionAndReceiptssRange transactionAndReceiptsRange, auto& storage)
        {
            m_chunkIndex = chunkIndex;
            m_lastChunkIndex = std::addressof(lastChunkIndex);
            m_transactionAndReceiptsRange = std::move(transactionAndReceiptsRange);
            m_storages.emplace(storage);
        }

        int64_t chunkIndex() { return m_chunkIndex; }
        auto count() { return RANGES::size(m_transactionAndReceiptsRange); }
        ChunkLocalStorage& localStorage() & { return m_storages->m_localStorage; }

        task::Task<bool> execute(protocol::IsBlockHeader auto const& blockHeader,
            auto& receiptFactory, auto& tableNamePool)
        {
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " executing...";
            Executor<decltype(m_storages->m_readWriteSetStorage)> executor(
                m_storages->m_readWriteSetStorage, receiptFactory, tableNamePool);
            for (auto&& [contextID, transaction, receipt] : m_transactionAndReceiptsRange)
            {
                if (m_chunkIndex >= *m_lastChunkIndex)
                {
                    PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " execute aborted";
                    co_return m_finished;
                }
                *receipt = co_await executor.execute(blockHeader, *transaction, contextID);
            }

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " execute finished";
            m_finished = true;
            co_return m_finished;
        }

        void detectRAW(ChunkExecuteStatus* prev, ChunkExecuteStatus* next)
        {
            if (prev == nullptr && next == nullptr)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty prev and next!"});
            }

            // Detected RAW
            if (m_chunkIndex < *m_lastChunkIndex)
            {
                bool expected = false;
                if (prev && prev->m_finished &&
                    prev->m_compareNext.compare_exchange_strong(expected, true))
                {
                    m_comparePrev = true;
                    if (prev->m_storages->m_readWriteSetStorage.hasRAWIntersection(
                            m_storages->m_readWriteSetStorage))
                    {
                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Detected left RAW intersection, abort: " << m_chunkIndex;
                        decreaseNumber(*m_lastChunkIndex, m_chunkIndex);
                        return;
                    }
                }
            }

            if (m_chunkIndex < *m_lastChunkIndex)
            {
                bool expected = false;
                if (next && next->m_finished &&
                    m_compareNext.compare_exchange_strong(expected, true))
                {
                    next->m_comparePrev = true;
                    if (m_storages->m_readWriteSetStorage.hasRAWIntersection(
                            next->m_storages->m_readWriteSetStorage))
                    {
                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Detected right RAW intersection, abort: " << m_chunkIndex + 1;
                        decreaseNumber(*m_lastChunkIndex, m_chunkIndex + 1);
                        return;
                    }
                }
            }
        }

        void merge(ChunkExecuteStatus& from)
        {
            m_storages->m_localStorageView.mutableStorage().merge(
                from.m_storages->m_localStorageView.mutableStorage(), false);
        }

        static void decreaseNumber(std::atomic_int64_t& number, int64_t target)
        {
            auto current = number.load();
            while (current > target && !number.compare_exchange_strong(current, target))
            {}
        }
    };

    task::Task<void> serialExecute(protocol::IsBlockHeader auto const& blockHeader,
        auto& receiptFactory, auto& tableNamePool, RANGES::range auto&& transactionAndReceipts,
        auto& storage)
    {
        Executor<std::remove_cvref_t<decltype(storage)>> executor(
            storage, receiptFactory, tableNamePool);
        for (auto&& [contextID, transaction, receipt] : transactionAndReceipts)
        {
            *receipt = co_await executor.execute(blockHeader, *transaction, contextID);
        }
    }

public:
    using SchedulerBaseImpl<MultiLayerStorage, Executor>::receiptFactory;
    using SchedulerBaseImpl<MultiLayerStorage, Executor>::tableNamePool;

    SchedulerParallelImpl(MultiLayerStorage& multiLayerStorage,
        protocol::TransactionReceiptFactory& receiptFactory,
        transaction_executor::TableNamePool& tableNamePool)
      : SchedulerBaseImpl<MultiLayerStorage, Executor>(
            multiLayerStorage, receiptFactory, tableNamePool),
        m_asyncTaskGroup(std::make_unique<tbb::task_group>())
    {}

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        auto storageView = multiLayerStorage().fork(true);
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        receipts.resize(RANGES::size(transactions));

        size_t offset = 0;
        auto chunkSize = m_chunkSize;
        while (offset < RANGES::size(transactions))
        {
            auto transactionAndReceiptsChunks =
                RANGES::zip_view(RANGES::iota_view(0LU, (size_t)RANGES::size(transactions)),
                    transactions | RANGES::views::addressof, receipts | RANGES::views::addressof) |
                RANGES::views::drop(offset) | RANGES::views::chunk(chunkSize);
            using ChunkType =
                ChunkExecuteStatus<RANGES::range_value_t<decltype(transactionAndReceiptsChunks)>>;
            std::vector<ChunkType> executeChunks(
                (size_t)RANGES::size(transactionAndReceiptsChunks));

            std::atomic_int64_t lastChunkIndex =
                (int64_t)RANGES::size(transactionAndReceiptsChunks);
            int64_t chunkIndex = 0;
            executeChunks[chunkIndex].init(
                chunkIndex, lastChunkIndex, transactionAndReceiptsChunks[chunkIndex], storageView);

            auto executeIt = RANGES::begin(executeChunks);
            auto finishedIt = RANGES::begin(executeChunks);
            tbb::task_group mergeTasks;
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing...";
            tbb::parallel_pipeline(m_maxToken,
                tbb::make_filter<void, std::optional<RANGES::iterator_t<decltype(executeChunks)>>>(
                    tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control)
                        -> std::optional<RANGES::iterator_t<decltype(executeChunks)>> {
                        if (chunkIndex >= lastChunkIndex)
                        {
                            control.stop();
                            return {executeIt++};
                        }

                        if ((size_t)chunkIndex != RANGES::size(transactionAndReceiptsChunks) - 1)
                        {
                            executeChunks[chunkIndex + 1].init(chunkIndex + 1, lastChunkIndex,
                                transactionAndReceiptsChunks[chunkIndex + 1], storageView);
                        }
                        ++chunkIndex;
                        return {executeIt++};
                    }) &
                    tbb::make_filter<std::optional<RANGES::iterator_t<decltype(executeChunks)>>,
                        std::optional<RANGES::iterator_t<decltype(executeChunks)>>>(
                        tbb::filter_mode::parallel,
                        [&](std::optional<RANGES::iterator_t<decltype(executeChunks)>> input)
                            -> std::optional<RANGES::iterator_t<decltype(executeChunks)>> {
                            if (!input)
                            {
                                return {};
                            }

                            auto& chunkIt = *input;
                            if (!task::syncWait(chunkIt->execute(
                                    blockHeader, receiptFactory(), tableNamePool())))
                            {
                                return {};
                            }

                            if (RANGES::size(transactionAndReceiptsChunks) > 1)
                            {
                                auto chunkIndex = chunkIt->chunkIndex();
                                auto prevIt = chunkIt;
                                auto prev =
                                    (chunkIndex == 0 ? nullptr : std::addressof(*(--prevIt)));

                                auto nextIt = chunkIt;
                                auto next =
                                    (chunkIndex == RANGES::size(transactionAndReceiptsChunks) - 1 ?
                                            nullptr :
                                            std::addressof(*(++nextIt)));
                                chunkIt->detectRAW(prev, next);
                            }

                            return input;
                        }) &
                    tbb::make_filter<std::optional<RANGES::iterator_t<decltype(executeChunks)>>,
                        void>(tbb::filter_mode::serial_in_order,
                        [&](std::optional<RANGES::iterator_t<decltype(executeChunks)>> input) {
                            if (!input || (*input)->chunkIndex() >= lastChunkIndex)
                            {
                                return;
                            }
                            offset += (*input)->count();
                            ++finishedIt;

                            auto index = (*input)->chunkIndex();
                            if (index > 0 && (index + 2) % 2 != 0)
                            {
                                mergeTasks.run([&executeChunks, index = (*input)->chunkIndex()]() {
                                    executeChunks[index].merge(executeChunks[index - 1]);
                                });
                            }
                        }));
            mergeTasks.wait();

            MergeRangeType reduceRange =
                RANGES::subrange(RANGES::begin(executeChunks), finishedIt) |
                RANGES::views::chunk(2) |
                RANGES::views::transform(
                    [](auto&& input) -> typename MultiLayerStorage::MutableStorage* {
                        if (RANGES::size(input) > 1)
                        {
                            auto& mutableStorage = input[1].localStorage().mutableStorage();
                            return std::addressof(mutableStorage);
                        }
                        auto& mutableStorage = input[0].localStorage().mutableStorage();
                        return std::addressof(mutableStorage);
                    });

            auto outRange = mergeStorages(reduceRange);
            storageView.mutableStorage().merge(**(outRange.begin()), true);
            chunkSize = std::min(chunkSize * 2, (size_t)RANGES::size(transactions));

            m_asyncTaskGroup->run([executeChunks = std::move(executeChunks)]() {});
        }

        // Still have transactions, execute it serially
        // if (chunkIt != RANGES::end(executeChunks))
        // {
        //     auto startOffset =
        //         RANGES::distance(RANGES::begin(executeChunks), chunkIt) * m_chunkSize;
        //     co_await serialExecute(blockHeader, startOffset, receiptFactory(), tableNamePool(),
        //         transactionAndReceiptsChunks | RANGES::views::drop(startOffset),
        //         localMultiLayerStorage);
        // }

        co_return receipts;
    }

    using MergeRangeType = RANGES::any_view<typename MultiLayerStorage::MutableStorage*,
        RANGES::category::mask | RANGES::category::sized>;
    MergeRangeType mergeStorages(MergeRangeType& range)
    {
        auto inputRange = range | RANGES::views::chunk(2);
        MergeRangeType outputRange =
            inputRange | RANGES::views::transform(
                             [](auto&& input) -> typename MultiLayerStorage::MutableStorage* {
                                 typename MultiLayerStorage::MutableStorage* storage = nullptr;
                                 for (auto* ptr : input)
                                 {
                                     storage = ptr;
                                 }
                                 return storage;
                             });

        tbb::task_group mergeGroup;
        for (auto&& subrange : inputRange)
        {
            mergeGroup.run([subrange]() {
                typename MultiLayerStorage::MutableStorage* storage = nullptr;
                for (auto* ptr : subrange)
                {
                    if (!storage)
                    {
                        storage = ptr;
                    }
                    else
                    {
                        ptr->merge(*storage, false);
                    }
                }
            });
        }
        mergeGroup.wait();

        if (RANGES::size(outputRange) > 1)
        {
            return mergeStorages(outputRange);
        }
        return outputRange;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxToken(size_t maxToken) { m_maxToken = maxToken; }
};
}  // namespace bcos::transaction_scheduler