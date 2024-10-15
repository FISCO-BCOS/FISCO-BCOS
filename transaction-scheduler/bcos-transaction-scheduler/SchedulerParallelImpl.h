#pragma once

#include "GC.h"
#include "MultiLayerStorage.h"
#include "ReadWriteSetStorage.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/ITTAPI.h"
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <boost/pool/object_pool.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <range/v3/view/enumerate.hpp>
#include <type_traits>

namespace bcos::transaction_scheduler
{

#define PARALLEL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARALLEL_SCHEDULER")

template <class MutableStorage, class Storage>
struct StorageTrait
{
    using LocalStorageView = View<MutableStorage, void, Storage>;
    using LocalReadWriteSetStorage =
        ReadWriteSetStorage<LocalStorageView, transaction_executor::StateKey>;
};

constexpr static auto EXECUTOR_STACK = 2048;
template <class CoroType>
struct ExecutionContext
{
    ExecutionContext()  // NOLINT
      : m_resource(m_stack.data(), m_stack.size()), m_allocator(std::addressof(m_resource)){};

    void init(int contextID, const protocol::Transaction& transaction,
        protocol::TransactionReceipt::Ptr& receipt)
    {
        this->contextID = contextID;
        this->transaction = std::addressof(transaction);
        this->receipt = std::addressof(receipt);
    }

    std::array<std::byte, EXECUTOR_STACK> m_stack;
    std::pmr::monotonic_buffer_resource m_resource;
    std::pmr::polymorphic_allocator<> m_allocator;

    int contextID;
    const protocol::Transaction* transaction;
    protocol::TransactionReceipt::Ptr* receipt;
    std::optional<CoroType> coro;
    std::optional<decltype(coro->begin())> iterator;
};

template <class MutableStorage, class Storage, class Executor, class ContextRange>
class ChunkStatus
{
private:
    int64_t m_chunkIndex = 0;
    std::reference_wrapper<boost::atomic_flag const> m_hasRAW;
    ContextRange m_contextRange;
    std::reference_wrapper<Executor> m_executor;
    typename StorageTrait<MutableStorage, Storage>::LocalStorageView m_storageView;
    typename StorageTrait<MutableStorage, Storage>::LocalReadWriteSetStorage m_readWriteSetStorage;

public:
    ChunkStatus(int64_t chunkIndex, boost::atomic_flag const& hasRAW, ContextRange contextRange,
        Executor& executor, auto& storage)
      : m_chunkIndex(chunkIndex),
        m_hasRAW(hasRAW),
        m_contextRange(std::move(contextRange)),
        m_executor(executor),
        m_storageView(storage),
        m_readWriteSetStorage(m_storageView)
    {
        newMutable(m_storageView);
    }

    int64_t chunkIndex() const { return m_chunkIndex; }
    auto count() const { return RANGES::size(m_contextRange); }
    auto& storageView() & { return m_storageView; }
    auto& readWriteSetStorage() & { return m_readWriteSetStorage; }

    void executeStep1(
        protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK1);
        for (auto&& [index, context] : RANGES::views::enumerate(m_contextRange))
        {
            if (m_hasRAW.get().test())
            {
                PARALLEL_SCHEDULER_LOG(DEBUG)
                    << "Chunk: " << m_chunkIndex << " aborted in step1, executed " << index
                    << " transactions";
                break;
            }
            context.init(index, *context.transaction, *context.receipt);

            context.coro.emplace(transaction_executor::execute3Step(m_executor.get(),
                m_readWriteSetStorage, blockHeader, *context.transaction, context.contextID,
                ledgerConfig, task::tbb::syncWait, std::allocator_arg, context.m_allocator));
            context.iterator.emplace(context.coro->begin());
            *context.receipt = *(*context.iterator);
        }
    }

    void executeStep2()
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK2);
        for (auto&& [index, context] : RANGES::views::enumerate(m_contextRange))
        {
            if (m_hasRAW.get().test())
            {
                PARALLEL_SCHEDULER_LOG(DEBUG)
                    << "Chunk: " << m_chunkIndex << " aborted in step2, executed " << index
                    << " transactions";
                break;
            }
            if (!(*context.receipt) && *context.iterator != context.coro->end())
            {
                *context.receipt = *(++(*context.iterator));
            }
        }
    }

    void executeStep3()
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().EXECUTE_CHUNK3);
        for (auto& context : m_contextRange)
        {
            if (!(*context.receipt) && *context.iterator != context.coro->end())
            {
                *context.receipt = *(++(*context.iterator));
            }
        }
    }
};

constexpr static auto DEFAULT_GRAIN_SIZE = 16UL;
constexpr static auto DEFAULT_MAX_CONCURRENCY = 8UL;

template <class MutableStorageType>
class SchedulerParallelImpl
{
    constexpr static auto DEFAULT_GRAIN_SIZE = 16UL;
    constexpr static auto DEFAULT_MAX_CONCURRENCY = 8UL;

public:
    constexpr static bool isSchedulerParallelImpl = true;
    using MutableStorage = MutableStorageType;

    GC m_gc;
    size_t m_grainSize = DEFAULT_GRAIN_SIZE;
    size_t m_maxConcurrency = DEFAULT_MAX_CONCURRENCY;
};

template <class Scheduler>
concept IsSchedulerParallelImpl = Scheduler::isSchedulerParallelImpl;

template <IsSchedulerParallelImpl SchedulerParallelImpl>
task::Task<void> mergeLastStorage(
    SchedulerParallelImpl& scheduler, auto& storage, auto&& lastStorage)
{
    ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().MERGE_LAST_CHUNK);
    PARALLEL_SCHEDULER_LOG(DEBUG) << "Final merge lastStorage";
    co_await storage2::merge(storage, std::forward<decltype(lastStorage)>(lastStorage));
}

template <IsSchedulerParallelImpl SchedulerParallelImpl>
size_t executeSinglePass(SchedulerParallelImpl& scheduler, auto& storage, auto& executor,
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
    ::ranges::random_access_range auto& contexts, size_t chunkSize)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().SINGLE_PASS);

    const auto count = RANGES::size(contexts);
    ReadWriteSetStorage<decltype(storage), transaction_executor::StateKey> writeSet(storage);

    using Chunk = ChunkStatus<typename SchedulerParallelImpl::MutableStorage,
        std::decay_t<decltype(storage)>, std::decay_t<decltype(executor)>,
        decltype(RANGES::subrange<RANGES::iterator_t<decltype(contexts)>>(contexts))>;

    boost::atomic_flag hasRAW;
    typename SchedulerParallelImpl::MutableStorage lastStorage;
    auto contextChunks = RANGES::views::chunk(contexts, chunkSize);

    std::atomic_size_t offset = 0;
    std::atomic_size_t chunkIndex = 0;

    tbb::task_group_context context;
    // 五级流水线：分片准备、并行执行、检测RAW冲突&合并读写集、生成回执、合并storage
    // Five-stage pipeline: shard preparation, parallel execution, detection of RAW
    // conflicts & merging read/write sets, generating receipts, and merging storage
    tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
        tbb::make_filter<void, std::unique_ptr<Chunk>>(tbb::filter_mode::serial_in_order,
            [&](tbb::flow_control& control) -> std::unique_ptr<Chunk> {
                if (chunkIndex >= RANGES::size(contextChunks) || hasRAW.test())
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
                        chunk->executeStep1(blockHeader, ledgerConfig);
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
                        chunk->executeStep2();
                    }

                    return chunk;
                }) &
            tbb::make_filter<std::unique_ptr<Chunk>, std::unique_ptr<Chunk>>(
                tbb::filter_mode::serial_in_order,
                [&](std::unique_ptr<Chunk> chunk) -> std::unique_ptr<Chunk> {
                    ittapi::Report report1(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                        ittapi::ITT_DOMAINS::instance().STAGE_4);
                    if (hasRAW.test())
                    {
                        scheduler.m_gc.collect(std::move(chunk));
                        return {};
                    }

                    auto index = chunk->chunkIndex();
                    if (index > 0)
                    {
                        ittapi::Report report2(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().DETECT_RAW);
                        if (hasRAWIntersection(writeSet, chunk->readWriteSetStorage()))
                        {
                            hasRAW.test_and_set();
                            PARALLEL_SCHEDULER_LOG(DEBUG) << "Detected RAW Intersection:" << index;
                            scheduler.m_gc.collect(std::move(chunk));
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
                        ittapi::ITT_DOMAINS::instance().STAGE_5);
                    if (chunk)
                    {
                        chunk->executeStep3();
                    }

                    return chunk;
                }) &
            tbb::make_filter<std::unique_ptr<Chunk>, void>(tbb::filter_mode::serial_in_order,
                [&](std::unique_ptr<Chunk> chunk) {
                    if (chunk)
                    {
                        ittapi::Report report1(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_6);
                        offset += (size_t)chunk->count();
                        ittapi::Report report2(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().MERGE_CHUNK);
                        PARALLEL_SCHEDULER_LOG(DEBUG)
                            << "Merging storage... " << chunk->chunkIndex() << " | "
                            << chunk->count();
                        task::tbb::syncWait(storage2::merge(
                            lastStorage, std::move(mutableStorage(chunk->storageView()))));
                        scheduler.m_gc.collect(std::move(chunk));
                    }
                    else
                    {
                        context.cancel_group_execution();
                    }
                }),
        context);

    task::tbb::syncWait(mergeLastStorage(scheduler, storage, std::move(lastStorage)));
    scheduler.m_gc.collect(std::move(writeSet));
    if (offset < count)
    {
        PARALLEL_SCHEDULER_LOG(DEBUG)
            << "Start new chunk executing... " << offset << " | " << RANGES::size(contexts);
        auto nextView = RANGES::views::drop(contexts, offset);
        return 1 + executeSinglePass(scheduler, storage, executor, blockHeader, ledgerConfig,
                       nextView, chunkSize);
    }

    return 0;
}

template <IsSchedulerParallelImpl SchedulerParallelImpl>
task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
    tag_t<executeBlock> /*unused*/, SchedulerParallelImpl& scheduler, auto& storage, auto& executor,
    protocol::BlockHeader const& blockHeader,
    ::ranges::random_access_range auto const& transactions,
    ledger::LedgerConfig const& ledgerConfig)
{
    auto transactionCount = RANGES::size(transactions);
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().PARALLEL_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().PARALLEL_EXECUTE);
    std::vector<protocol::TransactionReceipt::Ptr> receipts(transactionCount);

    using Storage = std::decay_t<decltype(storage)>;
    using CoroType = std::invoke_result_t<transaction_executor::Execute3Step, decltype(executor),
        std::add_lvalue_reference_t<typename StorageTrait<
            typename SchedulerParallelImpl::MutableStorage, Storage>::LocalReadWriteSetStorage>,
        protocol::BlockHeader const&, protocol::Transaction const&, int,
        ledger::LedgerConfig const&, task::tbb::SyncWait>;

    std::vector<ExecutionContext<CoroType>> contexts(transactionCount);
    tbb::task_arena arena(
        tbb::task_arena::constraints{}.set_max_concurrency(scheduler.m_maxConcurrency));
    arena.execute([&]() {
        auto retryCount = executeSinglePass(scheduler, storage, executor, blockHeader, ledgerConfig,
            contexts, scheduler.m_grainSize);
        scheduler.m_gc.collect(std::move(contexts));
        PARALLEL_SCHEDULER_LOG(INFO) << "Parallel execute block retry count: " << retryCount;
    });

    co_return receipts;
}

}  // namespace bcos::transaction_scheduler