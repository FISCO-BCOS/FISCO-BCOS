#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/storage2/StorageMethods.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-task/TBBWait.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ITTAPI.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/partitioner.h>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

class SchedulerParallelImpl
{
    using ChunkStorage = storage2::memory_storage::MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
    std::unique_ptr<tbb::task_group> m_asyncTaskGroup;
    constexpr static size_t MIN_CHUNK_SIZE = 32;
    size_t m_chunkSize = MIN_CHUNK_SIZE;
    size_t m_maxToken = 0;

    template <class Storage, class Executor, class Range>
    class ChunkStatus
    {
    private:
        auto forkAndMutable(auto& storage)
        {
            storage.newMutable();
            auto view = storage.fork(true);
            return view;
        }

        int64_t m_chunkIndex = 0;
        std::atomic_int64_t* m_lastChunkIndex = nullptr;
        Range m_transactionAndReceiptsRange;
        Executor& m_executor;
        MultiLayerStorage<ChunkStorage, void, Storage> m_localStorage;
        decltype(m_localStorage.fork(true)) m_localStorageView;
        ReadWriteSetStorage<decltype(m_localStorageView), transaction_executor::StateKey>
            m_localReadWriteSetStorage;

    public:
        ChunkStatus(int64_t chunkIndex, std::atomic_int64_t& lastChunkIndex,
            Range transactionAndReceiptsRange, Executor& executor, auto& storage)
          : m_chunkIndex(chunkIndex),
            m_lastChunkIndex(std::addressof(lastChunkIndex)),
            m_transactionAndReceiptsRange(transactionAndReceiptsRange),
            m_executor(executor),
            m_localStorage(storage),
            m_localStorageView(forkAndMutable(m_localStorage)),
            m_localReadWriteSetStorage(m_localStorageView)
        {}

        int64_t chunkIndex() { return m_chunkIndex; }
        auto count() { return RANGES::size(m_transactionAndReceiptsRange); }
        decltype(m_localStorage)& localStorage() & { return m_localStorage; }
        auto& readWriteSetStorage() & { return m_localReadWriteSetStorage; }

        task::Task<void> execute(
            protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig)
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK);
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " executing...";
            for (auto&& [contextID, transaction, receipt] : m_transactionAndReceiptsRange)
            {
                if (m_chunkIndex >= *m_lastChunkIndex)
                {
                    PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " execute aborted";
                    co_return;
                }
                *receipt = co_await transaction_executor::executeTransaction(m_executor,
                    m_localReadWriteSetStorage, blockHeader, *transaction, contextID, ledgerConfig);
            }

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " execute finished";
        }
    };

public:
    SchedulerParallelImpl(const SchedulerParallelImpl&) = delete;
    SchedulerParallelImpl(SchedulerParallelImpl&&) noexcept = default;
    SchedulerParallelImpl& operator=(const SchedulerParallelImpl&) = delete;
    SchedulerParallelImpl& operator=(SchedulerParallelImpl&&) noexcept = default;

    SchedulerParallelImpl() : m_asyncTaskGroup(std::make_unique<tbb::task_group>()) {}
    ~SchedulerParallelImpl() noexcept { m_asyncTaskGroup->wait(); }

    void setChunkSize(size_t chunkSize) { m_chunkSize = chunkSize; }
    void setMaxToken(size_t maxToken) { m_maxToken = maxToken; }

private:
    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        tag_t<executeBlock> /*unused*/, SchedulerParallelImpl& scheduler, auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().PARALLEL_EXECUTE);
        std::vector<protocol::TransactionReceipt::Ptr> receipts(RANGES::size(transactions));

        size_t offset = 0;
        size_t retryCount = 0;
        while (offset < RANGES::size(transactions))
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SINGLE_PASS);

            auto currentTransactionAndReceipts =
                RANGES::views::iota(offset, (size_t)RANGES::size(receipts)) |
                RANGES::views::transform([&](auto index) {
                    return std::make_tuple(index, std::addressof(transactions[index]),
                        std::addressof(receipts[index]));
                });
            std::atomic_int64_t lastChunkIndex{std::numeric_limits<int64_t>::max()};
            int64_t chunkIndex = 0;
            ReadWriteSetStorage<decltype(storage), transaction_executor::StateKey> writeSet(
                storage);
            auto chunks =
                currentTransactionAndReceipts | RANGES::views::chunk(scheduler.m_chunkSize);
            using Chunk = SchedulerParallelImpl::ChunkStatus<std::decay_t<decltype(storage)>,
                std::decay_t<decltype(executor)>, RANGES::range_value_t<decltype(chunks)>>;

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing... " << offset << " | "
                                          << RANGES::size(currentTransactionAndReceipts);
            ChunkStorage lastStorage;
            tbb::parallel_pipeline(scheduler.m_maxToken == 0 ? std::thread::hardware_concurrency() :
                                                               scheduler.m_maxToken,
                tbb::make_filter<void, std::unique_ptr<Chunk>>(tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) -> std::unique_ptr<Chunk> {
                        if (chunkIndex >= RANGES::size(chunks))
                        {
                            control.stop();
                            return {};
                        }
                        PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk: " << chunkIndex;
                        auto chunk = std::make_unique<Chunk>(
                            chunkIndex, lastChunkIndex, chunks[chunkIndex], executor, storage);
                        ++chunkIndex;
                        return chunk;
                    }) &
                    tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                        tbb::filter_mode::parallel,
                        [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                            auto index = chunk->chunkIndex();
                            if (index >= lastChunkIndex)
                            {
                                return chunk;
                            }
                            task::tbb::syncWait(chunk->execute(blockHeader, ledgerConfig));

                            return chunk;
                        }) &
                    tbb::make_filter<std::unique_ptr<Chunk>, void>(
                        tbb::filter_mode::serial_in_order, [&](std::unique_ptr<Chunk> chunk) {
                            auto index = chunk->chunkIndex();
                            if (index >= lastChunkIndex)
                            {
                                return;
                            }

                            if (index > 0)
                            {
                                ittapi::Report report(
                                    ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                    ittapi::ITT_DOMAINS::instance().DETECT_RAW);
                                if (writeSet.hasRAWIntersection(chunk->readWriteSetStorage()))
                                {
                                    PARALLEL_SCHEDULER_LOG(DEBUG)
                                        << "Detected RAW Intersection:" << index;
                                    lastChunkIndex = index;
                                    scheduler.m_asyncTaskGroup->run(
                                        [chunk = std::move(chunk)]() {});
                                    return;
                                }
                            }

                            offset += (size_t)chunk->count();
                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                << "Merging... " << index << " | " << chunk->count();
                            tbb::parallel_invoke(
                                [&]() {
                                    ittapi::Report report(
                                        ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                        ittapi::ITT_DOMAINS::instance().MERGE_RWSET);
                                    writeSet.mergeWriteSet(chunk->readWriteSetStorage());
                                },
                                [&]() {
                                    ittapi::Report report(
                                        ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                        ittapi::ITT_DOMAINS::instance().MERGE_CHUNK);
                                    task::tbb::syncWait(storage2::merge(
                                        chunk->localStorage().mutableStorage(), lastStorage));
                                });
                            scheduler.m_asyncTaskGroup->run([chunk = std::move(chunk)]() {});
                        }));

            ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().MERGE_LAST_CHUNK);
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Final merge lastStorage";
            co_await storage2::merge(lastStorage, storage);

            scheduler.m_asyncTaskGroup->run(
                [lastStorage = std::move(lastStorage), readWriteSet = std::move(writeSet)]() {});
            ++retryCount;
        }

        PARALLEL_SCHEDULER_LOG(INFO)
            << "Parallel scheduler execute finished, retry counts: " << retryCount;

        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler