#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/TransactionReceipt.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"

namespace bcos::transaction_scheduler
{

struct ExecuteBlock
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
        protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions,
        auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, scheduler, storage,
            executor, blockHeader, transactions, std::forward<decltype(args)>(args)...))>>
        requires RANGES::range<task::AwaitableReturnType<decltype(tag_invoke(*this, scheduler,
                     storage, executor, blockHeader, transactions,
                     std::forward<decltype(args)>(args)...))>> &&
                 std::convertible_to<
                     RANGES::range_value_t<task::AwaitableReturnType<decltype(tag_invoke(*this,
                         scheduler, storage, executor, blockHeader, transactions,
                         std::forward<decltype(args)>(args)...))>>,
                     std::shared_ptr<protocol::TransactionReceipt>>
    {
        co_return co_await tag_invoke(*this, scheduler, storage, executor, blockHeader,
            transactions, std::forward<decltype(args)>(args)...);
    }
};
inline constexpr ExecuteBlock executeBlock{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_scheduler