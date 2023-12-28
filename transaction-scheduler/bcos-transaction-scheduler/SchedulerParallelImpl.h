#pragma once

#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-task/TBBWait.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ITTAPI.h>
#include <oneapi/tbb/blocked_range.h>
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
    template <class Storage, class Executor, class ExecutionRange>
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
        boost::atomic_flag const& m_hasRAW;
        ExecutionRange m_transactionAndReceiptsRange;
        Executor& m_executor;
        MultiLayerStorage<typename Storage::MutableStorage, void, Storage> m_localStorage;
        decltype(m_localStorage.fork(true)) m_localStorageView;
        ReadWriteSetStorage<decltype(m_localStorageView), transaction_executor::StateKey>
            m_localReadWriteSetStorage;

    public:
        ChunkStatus(int64_t chunkIndex, boost::atomic_flag const& lastChunkIndex,
            ExecutionRange transactionAndReceiptsRange, Executor& executor, auto& storage)
          : m_chunkIndex(chunkIndex),
            m_hasRAW(lastChunkIndex),
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
                if (!m_hasRAW.test())
                {
                    *receipt = co_await transaction_executor::executeTransaction(m_executor,
                        m_localReadWriteSetStorage, blockHeader, *transaction, contextID,
                        ledgerConfig, task::tbb::syncWait);
                }
            }

            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk " << m_chunkIndex << " execute finished";
        }
    };

public:
    SchedulerParallelImpl(const SchedulerParallelImpl&) = delete;
    SchedulerParallelImpl(SchedulerParallelImpl&&) noexcept = default;
    SchedulerParallelImpl& operator=(const SchedulerParallelImpl&) = delete;
    SchedulerParallelImpl& operator=(SchedulerParallelImpl&&) noexcept = default;

    SchedulerParallelImpl() = default;
    ~SchedulerParallelImpl() noexcept = default;

private:
    constexpr static auto TRANSACTION_GRAIN_SIZE = 32;

    static task::Task<void> mergeLastStorage(
        SchedulerParallelImpl& scheduler, auto& storage, auto&& lastStorage)
    {
        ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().MERGE_LAST_CHUNK);
        PARALLEL_SCHEDULER_LOG(DEBUG) << "Final merge lastStorage";
        co_await storage2::merge(storage, std::forward<decltype(lastStorage)>(lastStorage));
    }

    static void executeSinglePass(SchedulerParallelImpl& scheduler, auto& storage, auto& executor,
        protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions,
        ledger::LedgerConfig const& ledgerConfig, size_t offset,
        std::vector<protocol::TransactionReceipt::Ptr>& receipts)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().SINGLE_PASS);

        auto count = RANGES::size(transactions);
        auto currentTransactionAndReceipts =
            RANGES::views::iota(offset, (size_t)RANGES::size(receipts)) |
            RANGES::views::transform([&](auto index) {
                return std::make_tuple(
                    index, std::addressof(transactions[index]), std::addressof(receipts[index]));
            });

        int64_t chunkIndex = 0;
        ReadWriteSetStorage<decltype(storage), transaction_executor::StateKey> writeSet(storage);
        using Chunk = SchedulerParallelImpl::ChunkStatus<std::decay_t<decltype(storage)>,
            std::decay_t<decltype(executor)>,
            decltype(RANGES::subrange<RANGES::iterator_t<decltype(currentTransactionAndReceipts)>>(
                currentTransactionAndReceipts))>;
        using ChunkStorage = typename std::decay_t<decltype(storage)>::MutableStorage;
        PARALLEL_SCHEDULER_LOG(DEBUG) << "Start new chunk executing... " << offset << " | "
                                      << RANGES::size(currentTransactionAndReceipts);

        boost::atomic_flag chunkFinished;
        boost::atomic_flag hasRAW;
        auto makeChunk = [&](tbb::blocked_range<int32_t> range) {
            PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk: " << chunkIndex;
            RANGES::subrange<RANGES::iterator_t<decltype(currentTransactionAndReceipts)>>
                executionRange(currentTransactionAndReceipts.begin() + range.begin(),
                    currentTransactionAndReceipts.begin() + range.end());
            return std::make_unique<Chunk>(chunkIndex++, hasRAW, executionRange, executor, storage);
        };

        tbb::blocked_range<int32_t> range(0,
            static_cast<int32_t>(RANGES::size(currentTransactionAndReceipts)),
            TRANSACTION_GRAIN_SIZE);
        ChunkStorage lastStorage;
        tbb::proportional_split split(1, tbb::this_task_arena::max_concurrency());

        // 四级流水线：分片、并行执行、检测RAW冲突&合并读写集、合并storage
        // Four-stage pipeline: split, parallel execution, detect RAW conflict and merge read/write
        // set, merge storage
        tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
            tbb::make_filter<void, std::unique_ptr<Chunk>>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) -> std::unique_ptr<Chunk> {
                    if (chunkFinished.test())
                    {
                        control.stop();
                        return {};
                    }
                    if (range.is_divisible())
                    {
                        auto newRange = tbb::blocked_range<int32_t>(range, split);
                        using std::swap;
                        swap(range, newRange);
                        return makeChunk(newRange);
                    }

                    chunkFinished.test_and_set();
                    return makeChunk(range);
                }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::parallel,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        if (chunk)
                        {
                            task::tbb::syncWait(chunk->execute(blockHeader, ledgerConfig));
                        }

                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::serial_in_order,
                    [&](std::unique_ptr<Chunk> chunk) {
                        auto index = chunk->chunkIndex();
                        if (hasRAW.test())
                        {
                            return std::unique_ptr<Chunk>{};
                        }

                        if (index > 0)
                        {
                            ittapi::Report report(
                                ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().DETECT_RAW);
                            if (writeSet.hasRAWIntersection(chunk->readWriteSetStorage()))
                            {
                                hasRAW.test_and_set();
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Detected RAW Intersection:" << index;
                                return std::unique_ptr<Chunk>{};
                            }
                        }

                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Merging rwset... " << index << " | " << chunk->count();
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().MERGE_RWSET);
                        writeSet.mergeWriteSet(chunk->readWriteSetStorage());
                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, void>(
                    tbb::filter_mode::serial_in_order, [&](std::unique_ptr<Chunk> chunk) {
                        if (!chunk)
                        {
                            return;
                        }

                        offset += (size_t)chunk->count();
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().MERGE_CHUNK);
                        task::tbb::syncWait(storage2::merge(
                            lastStorage, std::move(chunk->localStorage().mutableStorage())));
                    }));

        task::tbb::syncWait(mergeLastStorage(scheduler, storage, std::move(lastStorage)));
        if (offset < count)
        {
            executeSinglePass(scheduler, storage, executor, blockHeader, transactions, ledgerConfig,
                offset, receipts);
        }
    }

    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        tag_t<executeBlock> /*unused*/, SchedulerParallelImpl& scheduler, auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().PARALLEL_EXECUTE);
        std::vector<protocol::TransactionReceipt::Ptr> receipts(RANGES::size(transactions));
        executeSinglePass(
            scheduler, storage, executor, blockHeader, transactions, ledgerConfig, 0, receipts);
        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler