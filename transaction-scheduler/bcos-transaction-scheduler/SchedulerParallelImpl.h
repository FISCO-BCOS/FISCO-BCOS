#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "SchedulerBaseImpl.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/ITTAPI.h>
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
    std::unique_ptr<tbb::task_group> m_asyncTaskGroup;
    using ChunkLocalStorage =
        transaction_scheduler::MultiLayerStorage<typename MultiLayerStorage::MutableStorage, void,
            decltype(std::declval<MultiLayerStorage>().fork(true))>;
    using SchedulerBaseImpl<MultiLayerStorage, Executor>::multiLayerStorage;
    constexpr static size_t MIN_CHUNK_SIZE = 32;
    constexpr static size_t MAX_RETRY_COUNT = 30;

    size_t m_chunkSize = MIN_CHUNK_SIZE;
    size_t m_maxToken = 0;

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
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().CHUNK_EXECUTE);
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
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().DETECT_RAW);

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

        static void decreaseNumber(std::atomic_int64_t& number, int64_t target)
        {
            auto current = number.load();
            while (current > target && !number.compare_exchange_strong(current, target))
            {}
        }
    };

    task::Task<void> serialExecute(protocol::BlockHeader const& blockHeader,
        protocol::TransactionReceiptFactory& receiptFactory,
        transaction_executor::TableNamePool& tableNamePool,
        RANGES::range auto&& transactionAndReceipts, auto& storage)
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

    ~SchedulerParallelImpl() noexcept { m_asyncTaskGroup->wait(); }

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> execute(
        protocol::IsBlockHeader auto const& blockHeader,
        RANGES::input_range auto const& transactions)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().PARALLEL_EXECUTE);
        auto storageView = multiLayerStorage().fork(true);
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        receipts.resize(RANGES::size(transactions));

        size_t offset = 0;
        auto chunkSize = m_chunkSize;

        size_t retryCount = 0;
        auto transactionAndReceipts =
            RANGES::views::zip(RANGES::views::iota(0LU, (size_t)RANGES::size(transactions)),
                transactions | RANGES::views::addressof, receipts | RANGES::views::addressof);

        // while (offset < RANGES::size(transactions) && retryCount < MAX_RETRY_COUNT)
        while (offset < RANGES::size(transactions))
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SINGLE_PASS);

            auto transactionAndReceiptsChunks = transactionAndReceipts |
                                                RANGES::views::drop(offset) |
                                                RANGES::views::chunk(chunkSize);
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
            typename MultiLayerStorage::MutableStorage lastStorage;
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing...";

            tbb::parallel_pipeline(m_maxToken == 0 ? executeChunks.size() : m_maxToken,
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

                            auto index = (*input)->chunkIndex();
                            {
                                ittapi::Report report(
                                    ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                    ittapi::ITT_DOMAINS::instance().PIPELINE_MERGE_STORAGE);

                                task::syncWait(storage2::merge(
                                    executeChunks[index].localStorage().mutableStorage(),
                                    lastStorage));
                            }
                        }));
            {
                ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                    ittapi::ITT_DOMAINS::instance().FINAL_MERGE_STORAGE);
                if (storageView.mutableStorage().empty())
                {
                    storageView.mutableStorage().swap(lastStorage);
                }
                else
                {
                    co_await storage2::merge(lastStorage, storageView.mutableStorage());
                }
            }

            m_asyncTaskGroup->run([executeChunks = std::move(executeChunks),
                                      lastStorage = std::move(lastStorage)]() {});
            ++retryCount;
        }

        PARALLEL_SCHEDULER_LOG(DEBUG)
            << "Parallel scheduler execute finished, retry counts: " << retryCount;
        m_asyncTaskGroup->run([storageView = std::move(storageView)]() {});

        co_return receipts;
    }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxToken(size_t maxToken) { m_maxToken = maxToken; }
};
}  // namespace bcos::transaction_scheduler