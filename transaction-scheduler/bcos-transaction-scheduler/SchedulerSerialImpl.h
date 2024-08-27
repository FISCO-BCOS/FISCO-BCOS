#pragma once

#include "GC.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/TBBWait.h"
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_arena.h>
#include <tbb/task_arena.h>
#include <type_traits>

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
public:
    constexpr static auto MIN_TRANSACTION_GRAIN_SIZE = 16;
    GC m_gc;
};

task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
    tag_t<executeBlock> /*unused*/, SchedulerSerialImpl& scheduler, auto& storage, auto& executor,
    protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions,
    ledger::LedgerConfig const& ledgerConfig)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().SERIAL_EXECUTE);

    using CoroType = std::invoke_result_t<transaction_executor::Execute3Step, decltype(executor),
        decltype(storage), protocol::BlockHeader const&, protocol::Transaction const&, int,
        ledger::LedgerConfig const&, task::tbb::SyncWait>;
    struct ExecutionContext
    {
        std::optional<CoroType> coro;
        std::optional<decltype(coro->begin())> iterator;
        protocol::TransactionReceipt::Ptr receipt;
    };

    auto count = static_cast<int32_t>(RANGES::size(transactions));
    std::vector<ExecutionContext, tbb::cache_aligned_allocator<ExecutionContext>> contexts(count);

    auto chunks = RANGES::views::iota(
                      RANGES::range_size_t<decltype(transactions)>(0), RANGES::size(transactions)) |
                  RANGES::views::chunk(
                      std::max<size_t>((size_t)(count / tbb::this_task_arena::max_concurrency()),
                          (size_t)SchedulerSerialImpl::MIN_TRANSACTION_GRAIN_SIZE));
    using ChunkRange = RANGES::range_value_t<decltype(chunks)>;
    RANGES::range_size_t<decltype(transactions)> chunkIndex = 0;

    // 三级流水线，2个线程
    // Three-stage pipeline, with 2 threads
    static tbb::task_arena arena(2);
    arena.execute([&]() {
        tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
            tbb::make_filter<void, ChunkRange>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) -> ChunkRange {
                    if (chunkIndex >= RANGES::size(chunks))
                    {
                        control.stop();
                        return {};
                    }
                    ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                        ittapi::ITT_DOMAINS::instance().STAGE_1);

                    auto range = chunks[chunkIndex++];
                    for (auto i : range)
                    {
                        auto& [coro, iterator, receipt] = contexts[i];
                        coro.emplace(transaction_executor::execute3Step(executor, storage,
                            blockHeader, transactions[i], i, ledgerConfig, task::tbb::syncWait));
                        iterator.emplace(coro->begin());
                        receipt = *(*iterator);
                    }
                    return range;
                }) &
                tbb::make_filter<ChunkRange, ChunkRange>(tbb::filter_mode::serial_in_order,
                    [&](ChunkRange range) {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_2);
                        for (auto i : range)
                        {
                            auto& [coro, iterator, receipt] = contexts[i];
                            if (!receipt)
                            {
                                receipt = *(++(*iterator));
                            }
                        }
                        return range;
                    }) &
                tbb::make_filter<ChunkRange, void>(
                    tbb::filter_mode::serial_in_order, [&](ChunkRange range) {
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_3);
                        for (auto i : range)
                        {
                            auto& [coro, iterator, receipt] = contexts[i];
                            if (!receipt)
                            {
                                receipt = *(++(*iterator));
                            }
                            coro.reset();
                        }
                    }));
    });

    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    receipts.reserve(count);
    RANGES::move(RANGES::views::transform(
                     contexts, [](ExecutionContext& context) -> auto& { return context.receipt; }),
        RANGES::back_inserter(receipts));
    scheduler.m_gc.collect(std::move(contexts));
    co_return receipts;
}
}  // namespace bcos::transaction_scheduler