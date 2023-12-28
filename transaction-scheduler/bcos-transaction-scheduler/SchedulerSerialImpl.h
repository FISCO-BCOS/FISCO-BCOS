#pragma once

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/TBBWait.h"
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_arena.h>
#include <atomic>
#include <thread>

namespace bcos::transaction_scheduler
{

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
private:
    using Range = tbb::blocked_range<int32_t>;
    constexpr static auto TRANSACTION_GRAIN_SIZE = 32;

    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        tag_t<executeBlock> /*unused*/, SchedulerSerialImpl& /*unused*/, auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().SERIAL_EXECUTE);

        using CoroType = std::invoke_result_t<transaction_executor::Execute3Step,
            decltype(executor), decltype(storage), decltype(blockHeader),
            RANGES::range_value_t<decltype(transactions)>, int, decltype(ledgerConfig),
            task::tbb::SyncWait>;
        struct Context
        {
            std::optional<CoroType> coro;
            typename CoroType::Iterator iterator;
            protocol::TransactionReceipt::Ptr receipt;
        };

        auto count = static_cast<int32_t>(RANGES::size(transactions));
        std::vector<Context, tbb::cache_aligned_allocator<Context>> contexts(count);

        auto executeStep1 = [&](Range splitRange) {
            for (auto i = splitRange.begin(); i != splitRange.end(); ++i)
            {
                auto& [coro, iterator, receipt] = contexts[i];
                coro.emplace(transaction_executor::execute3Step(executor, storage, blockHeader,
                    transactions[i], i, ledgerConfig, task::tbb::syncWait));
                iterator = coro->begin();
                receipt = *iterator;
            }
        };

        Range range(0L, count, TRANSACTION_GRAIN_SIZE);
        // 三级流水线，2个线程
        // Three-stage pipeline, with 2 threads
        bool lastRange = false;
        static tbb::task_arena arena(2);
        arena.execute([&]() {
            tbb::parallel_pipeline(std::thread::hardware_concurrency(),
                tbb::make_filter<void, Range>(tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) {
                        if (lastRange)
                        {
                            control.stop();
                            return range;
                        }
                        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                            ittapi::ITT_DOMAINS::instance().STEP_1);

                        if (range.is_divisible())
                        {
                            auto newRange = Range(range, tbb::split{});
                            using std::swap;
                            swap(range, newRange);
                            executeStep1(newRange);
                            return newRange;
                        }

                        lastRange = true;
                        executeStep1(range);
                        return range;
                    }) &
                    tbb::make_filter<Range, Range>(tbb::filter_mode::serial_in_order,
                        [&](Range splitRange) {
                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STEP_2);
                            for (auto i = splitRange.begin(); i != splitRange.end(); ++i)
                            {
                                auto& [coro, iterator, receipt] = contexts[i];
                                if (!receipt)
                                {
                                    receipt = *(++iterator);
                                }
                            }
                            return splitRange;
                        }) &
                    tbb::make_filter<Range, void>(
                        tbb::filter_mode::serial_in_order, [&](Range splitRange) {
                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STEP_3);
                            for (auto i = splitRange.begin(); i != splitRange.end(); ++i)
                            {
                                auto& [coro, iterator, receipt] = contexts[i];
                                if (!receipt)
                                {
                                    receipt = *(++iterator);
                                }
                                coro.reset();
                            }
                        }));
        });

        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        receipts.reserve(count);
        RANGES::move(RANGES::views::transform(
                         contexts, [](Context & context) -> auto& { return context.receipt; }),
            RANGES::back_inserter(receipts));
        co_return receipts;
    }
};
}  // namespace bcos::transaction_scheduler