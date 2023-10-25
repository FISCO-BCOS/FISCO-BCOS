#pragma once

#include "../protocol/Block.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Ranges.h"
#include <oneapi/tbb/task_group.h>
#include <concepts>

namespace bcos::transaction_scheduler
{

struct Execute
{
    /**
     * @brief Function call operator for TransactionScheduler.
     *
     * @tparam Scheduler The type of the scheduler.
     * @tparam Storage The type of the storage.
     * @tparam Executor The type of the executor.
     * @tparam Transactions The type of the transactions.
     * @param scheduler The scheduler instance.
     * @param storage The storage instance.
     * @param executor The executor instance.
     * @param blockHeader The block header.
     * @param transactions The input range of transactions.
     * @return A task that returns the awaitable return type of the tag_invoke function.
     */
    auto operator()(auto& scheduler, auto& storage, auto& executor,
        protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, scheduler, storage, executor, blockHeader, transactions))>>
        requires RANGES::range<task::AwaitableReturnType<decltype(tag_invoke(
                     *this, scheduler, storage, executor, blockHeader, transactions))>> &&
                 std::convertible_to<
                     RANGES::range_value_t<task::AwaitableReturnType<decltype(tag_invoke(
                         *this, scheduler, storage, executor, blockHeader, transactions))>>,
                     protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke(
            *this, scheduler, storage, executor, blockHeader, transactions);
    }
};
inline constexpr Execute execute{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_scheduler