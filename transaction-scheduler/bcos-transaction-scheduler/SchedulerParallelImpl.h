#pragma once

#include "GC.h"
#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/ITTAPI.h"
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <range/v3/view/enumerate.hpp>

namespace bcos::scheduler_v1
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <class MutableStorage, class Storage>
struct StorageTrait
{
    using LocalStorageView = View<MutableStorage, void, Storage>;
    using LocalReadWriteSetStorage = ReadWriteSetStorage<LocalStorageView>;
};

struct ExecutionContext
{
    int32_t contextID;
    const protocol::Transaction* transaction;
    protocol::TransactionReceipt::Ptr* receipt;
};

template <class MutableStorage, class Storage,
    executor_v1::IsTransactionExecutor<Storage> TransactionExecutor, class Contexts>
class ChunkStatus
{
private:
    int64_t m_chunkIndex = 0;
    std::reference_wrapper<boost::atomic_flag const> m_hasRAW;
    Contexts m_contexts;
    std::reference_wrapper<TransactionExecutor> m_executor;
    typename StorageTrait<MutableStorage, Storage>::LocalStorageView m_storageView;
    typename StorageTrait<MutableStorage, Storage>::LocalReadWriteSetStorage m_readWriteSetStorage;
    std::vector<
        typename TransactionExecutor::template ExecuteContext<decltype(m_readWriteSetStorage)>>
        m_executeContexts;

public:
    ChunkStatus(int64_t chunkIndex, boost::atomic_flag const& hasRAW, Contexts contextRange,
        TransactionExecutor& executor, auto& storage)
      : m_chunkIndex(chunkIndex),
        m_hasRAW(hasRAW),
        m_contexts(std::move(contextRange)),
        m_executor(executor),
        m_storageView(storage),
        m_readWriteSetStorage(m_storageView)
    {
        m_storageView.newMutable();
    }

    int64_t chunkIndex() const { return m_chunkIndex; }
    auto count() const { return ::ranges::size(m_contexts); }
    auto& storageView() & { return m_storageView; }
    auto& readWriteSetStorage() & { return m_readWriteSetStorage; }

    task::Task<void> executeStep1(
        const protocol::BlockHeader& blockHeader, const ledger::LedgerConfig& ledgerConfig)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK3);
        m_executeContexts.reserve(::ranges::size(m_contexts));
        for (auto& context : m_contexts)
        {
            m_executeContexts.emplace_back(
                co_await m_executor.get().createExecuteContext(m_readWriteSetStorage, blockHeader,
                    *context.transaction, context.contextID, ledgerConfig, false));
        }
    }

    task::Task<void> executeStep2()
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK1);
        for (auto&& [index, executeContext] : ::ranges::views::enumerate(m_executeContexts))
        {
            if (m_hasRAW.get().test())
            {
                PARALLEL_SCHEDULER_LOG(DEBUG)
                    << "Chunk: " << m_chunkIndex << " aborted in step1, executed " << index
                    << " transactions";
                break;
            }
            co_await executeContext.template executeStep<0>();
        }
    }

    task::Task<void> executeStep3()
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK2);
        for (auto&& [index, executeContext] : ::ranges::views::enumerate(m_executeContexts))
        {
            if (m_hasRAW.get().test())
            {
                PARALLEL_SCHEDULER_LOG(DEBUG)
                    << "Chunk: " << m_chunkIndex << " aborted in step2, executed " << index
                    << " transactions";
                break;
            }
            co_await executeContext.template executeStep<1>();
        }
    }

    task::Task<void> executeStep4()
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK3);
        for (auto&& [context, executeContext] : ::ranges::views::zip(m_contexts, m_executeContexts))
        {
            *context.receipt = co_await executeContext.template executeStep<2>();
        }
    }
};

constexpr static auto DEFAULT_GRAIN_SIZE = 16UL;
constexpr static auto DEFAULT_MAX_CONCURRENCY = 8UL;

template <class MutableStorageType>
class SchedulerParallelImpl
{
public:
    constexpr static bool isSchedulerParallelImpl = true;
    using MutableStorage = MutableStorageType;

    size_t m_grainSize = DEFAULT_GRAIN_SIZE;
    size_t m_maxConcurrency = DEFAULT_MAX_CONCURRENCY;

    task::Task<void> mergeLastStorage(auto& storage, auto& lastStorage)
    {
        ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().MERGE_LAST_CHUNK);
        PARALLEL_SCHEDULER_LOG(DEBUG) << "Final merge lastStorage";
        co_await storage2::merge(storage, lastStorage);
    }

    template <class Storage, executor_v1::IsTransactionExecutor<Storage> TransactionExecutor>
    size_t executeSinglePass(Storage& storage, TransactionExecutor& executor,
        protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
        ::ranges::random_access_range auto& contexts, size_t chunkSize)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().SINGLE_PASS);

        const auto count = ::ranges::size(contexts);
        ReadWriteSetStorage<Storage> writeSet(storage);

        using Chunk = ChunkStatus<typename SchedulerParallelImpl::MutableStorage, Storage,
            TransactionExecutor,
            decltype(::ranges::subrange<::ranges::iterator_t<decltype(contexts)>>(contexts))>;

        boost::atomic_flag hasRAW;
        typename SchedulerParallelImpl::MutableStorage lastStorage;
        auto contextChunks = ::ranges::views::chunk(contexts, chunkSize);

        std::atomic_size_t offset = 0;
        std::atomic_size_t chunkIndex = 0;

        tbb::task_group_context context;
        // 七级流水线：生成分片、准备执行、第一段执行、第二段执行、检测RAW冲突&合并读写集、结束执行、合并storage
        // Seven-stage pipeline: shard preparation, parallel execution, detection of RAW
        // conflicts & merging read/write sets, generating receipts, and merging storage
        tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
            tbb::make_filter<void, std::unique_ptr<Chunk>>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) -> std::unique_ptr<Chunk> {
                    if (chunkIndex >= ::ranges::size(contextChunks) || hasRAW.test())
                    {
                        control.stop();
                        return {};
                    }

                    ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                        ittapi::ITT_DOMAINS::instance().STAGE_1);
                    PARALLEL_SCHEDULER_LOG(DEBUG) << "Chunk: " << chunkIndex;
                    auto chunk = std::make_unique<Chunk>(
                        chunkIndex, hasRAW, contextChunks[chunkIndex], executor, storage);
                    ++chunkIndex;
                    return chunk;
                }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::parallel,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_2);
                        if (chunk && !hasRAW.test())
                        {
                            task::tbb::syncWait(chunk->executeStep1(blockHeader, ledgerConfig));
                        }

                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::parallel,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_3);
                        if (chunk && !hasRAW.test())
                        {
                            task::tbb::syncWait(chunk->executeStep2());
                        }

                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::parallel,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_4);
                        if (chunk && !hasRAW.test())
                        {
                            task::tbb::syncWait(chunk->executeStep3());
                        }

                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::serial_in_order,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        ittapi::Report report1(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_5);
                        if (hasRAW.test())
                        {
                            GC::collect(std::move(chunk));
                            return {};
                        }

                        auto index = chunk->chunkIndex();
                        if (index > 0)
                        {
                            ittapi::Report report2(
                                ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().DETECT_RAW);
                            if (hasRAWIntersection(writeSet, chunk->readWriteSetStorage()))
                            {
                                hasRAW.test_and_set();
                                PARALLEL_SCHEDULER_LOG(DEBUG)
                                    << "Detected RAW Intersection:" << index;
                                GC::collect(std::move(chunk));
                                return {};
                            }
                        }

                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Merging rwset... " << index << " | " << chunk->count();
                        ittapi::Report report3(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().MERGE_RWSET);
                        mergeWriteSet(writeSet, chunk->readWriteSetStorage());
                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                    tbb::filter_mode::parallel,
                    [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_6);
                        if (chunk)
                        {
                            task::tbb::syncWait(chunk->executeStep4());
                        }

                        return chunk;
                    }) &
                tbb::make_filter<std::unique_ptr<Chunk>, void>(tbb::filter_mode::serial_in_order,
                    [&](std::unique_ptr<Chunk> chunk) {
                        if (chunk)
                        {
                            ittapi::Report report1(
                                ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STAGE_7);
                            offset += (size_t)chunk->count();
                            ittapi::Report report2(
                                ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().MERGE_CHUNK);
                            PARALLEL_SCHEDULER_LOG(DEBUG)
                                << "Merging storage... " << chunk->chunkIndex() << " | "
                                << chunk->count();
                            task::tbb::syncWait(
                                storage2::merge(lastStorage, mutableStorage(chunk->storageView())));
                            GC::collect(std::move(chunk));
                        }
                        else
                        {
                            context.cancel_group_execution();
                        }
                    }),
            context);

        task::tbb::syncWait(mergeLastStorage(storage, lastStorage));
        GC::collect(std::move(writeSet));
        if (offset < count)
        {
            PARALLEL_SCHEDULER_LOG(DEBUG)
                << "Start new chunk executing... " << offset << " | " << ::ranges::size(contexts);
            auto nextView = ::ranges::views::drop(contexts, offset);
            return 1 + executeSinglePass(
                           storage, executor, blockHeader, ledgerConfig, nextView, chunkSize);
        }

        return 0;
    }

    template <class Storage, executor_v1::IsTransactionExecutor<Storage> TransactionExecutor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage& storage,
        TransactionExecutor& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::random_access_range auto const& transactions,
        ledger::LedgerConfig const& ledgerConfig)
    {
        auto transactionCount = ::ranges::size(transactions);
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().PARALLEL_EXECUTE);
        std::vector<protocol::TransactionReceipt::Ptr> receipts(transactionCount);

        std::vector<ExecutionContext> contexts;
        contexts.reserve(transactionCount);
        for (auto index : ranges::views::iota(0LU, transactionCount))
        {
            contexts.emplace_back(
                index, std::addressof(transactions[index]), std::addressof(receipts[index]));
        }

        tbb::task_arena arena(m_maxConcurrency, 1, tbb::task_arena::priority::high);
        arena.execute([&]() {
            auto retryCount = executeSinglePass(
                storage, executor, blockHeader, ledgerConfig, contexts, m_grainSize);
            GC::collect(std::move(contexts));
            PARALLEL_SCHEDULER_LOG(INFO) << "Parallel execute block retry count: " << retryCount;
        });

        co_return receipts;
    }
};

}  // namespace bcos::scheduler_v1