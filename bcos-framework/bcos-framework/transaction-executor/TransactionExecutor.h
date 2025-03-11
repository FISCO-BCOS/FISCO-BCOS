#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include <type_traits>
#include <utility>

namespace bcos::executor_v1
{

constexpr static auto EXECUTOR_STACK = 1400;

inline constexpr struct ExecuteTransaction
{
    /**
     * @brief Executes a transaction and returns a task that resolves to a transaction receipt.
     *
     * @tparam Executor The type of the executor.
     * @tparam Storage The type of the storage.
     * @tparam Args The types of additional arguments.
     * @param executor The executor instance.
     * @param storage The storage instance.
     * @param blockHeader The block header.
     * @param transaction The transaction to execute.
     * @param args Additional arguments.
     * @return A task that resolves to a transaction receipt.
     */
    auto operator()(auto& executor, auto& storage, const protocol::BlockHeader& blockHeader,
        const protocol::Transaction& transaction,
        auto&&... args) const -> task::Task<protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke(*this, executor, storage, blockHeader, transaction,
            std::forward<decltype(args)>(args)...);
    }
} executeTransaction{};

inline constexpr struct CreateExecuteContext
{
    auto operator()(auto& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, executor, storage,
            blockHeader, transaction, std::forward<decltype(args)>(args)...))>>
    {
        return tag_invoke(*this, executor, storage, blockHeader, transaction,
            std::forward<decltype(args)>(args)...);
    }
} createExecuteContext{};

inline constexpr struct ExecuteStep
{
    template <int step>
    auto operator()(
        auto& executeContext, auto&&... args) const -> task::Task<protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke<step>(
            *this, executeContext, std::forward<decltype(args)>(args)...);
    }
} executeStep{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::executor_v1
