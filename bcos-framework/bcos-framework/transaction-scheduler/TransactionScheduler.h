#pragma once

#include "../protocol/Block.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Ranges.h"
#include <oneapi/tbb/task_group.h>

namespace bcos::transaction_scheduler
{

struct Execute
{
    auto operator()(auto& scheduler, protocol::BlockHeader const& blockHeader,
        RANGES::input_range auto const& transactions) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, scheduler, blockHeader, transactions))>>
        requires RANGES::range<task::AwaitableReturnType<decltype(tag_invoke(
                     *this, scheduler, blockHeader, transactions))>> &&
                 std::convertible_to<
                     RANGES::range_value_t<task::AwaitableReturnType<decltype(tag_invoke(
                         *this, scheduler, blockHeader, transactions))>>,
                     protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke(*this, scheduler, blockHeader, transactions);
    }
};
inline constexpr Execute execute{};
struct Commit
{
    auto operator()(auto& scheduler, auto& ledger, auto& block, auto& transactions) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, scheduler, ledger, block, transactions))>>
    {
        co_return co_await tag_invoke(*this, scheduler, ledger, block, transactions);
    }
};
inline constexpr Commit commit{};
struct Call
{
    auto operator()(auto& scheduler, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, scheduler, blockHeader, transaction))>>
    {
        co_return co_await tag_invoke(*this, scheduler, blockHeader, transaction);
    }
};
inline constexpr Call call{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_scheduler