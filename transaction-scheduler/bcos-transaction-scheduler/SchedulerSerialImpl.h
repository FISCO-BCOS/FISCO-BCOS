#pragma once

#include "GC.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Trait.h"
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_arena.h>
#include <tbb/task_arena.h>

namespace bcos::scheduler_v1
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
public:
    constexpr static auto MIN_TRANSACTION_GRAIN_SIZE = 16;
};

task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
    tag_t<executeBlock> /*unused*/, SchedulerSerialImpl& scheduler, auto& storage, auto& executor,
    protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions,
    ledger::LedgerConfig const& ledgerConfig)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().SERIAL_EXECUTE);
    auto count = static_cast<int32_t>(RANGES::size(transactions));

    using ExecutionContext =
        task::AwaitableReturnType<std::invoke_result_t<executor_v1::CreateExecuteContext,
            decltype(executor), decltype(storage), protocol::BlockHeader const&,
            protocol::Transaction const&, int, ledger::LedgerConfig const&>>;
    std::vector<ExecutionContext, tbb::cache_aligned_allocator<ExecutionContext>> contexts;
    contexts.reserve(count);

    auto chunks = RANGES::views::iota(0, count) |
                  RANGES::views::chunk(
                      std::max<size_t>((size_t)(count / tbb::this_task_arena::max_concurrency()),
                          (size_t)SchedulerSerialImpl::MIN_TRANSACTION_GRAIN_SIZE));
    using ChunkRange = RANGES::range_value_t<decltype(chunks)>;
    RANGES::range_size_t<decltype(transactions)> chunkIndex = 0;

    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    receipts.reserve(count);

    // 四级流水线，3个线程
    // Four-stage pipeline, with 3 threads
    static tbb::task_arena arena(3, 1, tbb::task_arena::priority::high);
    arena.execute([&]() {
        tbb::parallel_pipeline(SchedulerSerialImpl::MIN_TRANSACTION_GRAIN_SIZE,
            tbb::make_filter<void, ChunkRange>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) -> ChunkRange {
                    return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                        if (chunkIndex >= RANGES::size(chunks))
                        {
                            control.stop();
                            co_return {};
                        }

                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STAGE_1);

                        auto range = chunks[chunkIndex++];
                        for (auto i : range)
                        {
                            contexts.emplace_back(
                                co_await executor_v1::createExecuteContext(executor,
                                    storage, blockHeader, transactions[i], i, ledgerConfig));
                        }
                        co_return range;
                    }());
                }) &
                tbb::make_filter<ChunkRange, ChunkRange>(tbb::filter_mode::serial_in_order,
                    [&](ChunkRange range) {
                        return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STAGE_2);
                            for (auto i : range)
                            {
                                auto& context = contexts[i];
                                co_await executor_v1::executeStep.operator()<0>(context);
                            }
                            co_return range;
                        }());
                    }) &
                tbb::make_filter<ChunkRange, ChunkRange>(tbb::filter_mode::serial_in_order,
                    [&](ChunkRange range) {
                        return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STAGE_3);
                            for (auto i : range)
                            {
                                auto& context = contexts[i];
                                co_await executor_v1::executeStep.operator()<1>(context);
                            }
                            co_return range;
                        }());
                    }) &
                tbb::make_filter<ChunkRange, void>(
                    tbb::filter_mode::serial_in_order, [&](ChunkRange range) {
                        task::tbb::syncWait([&]() -> task::Task<void> {
                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STAGE_4);
                            for (auto i : range)
                            {
                                auto& context = contexts[i];
                                receipts.emplace_back(
                                    co_await executor_v1::executeStep.operator()<2>(
                                        context));
                            }
                        }());
                    }));
    });

    GC::collect(std::move(contexts));
    co_return receipts;
}
}  // namespace bcos::scheduler_v1