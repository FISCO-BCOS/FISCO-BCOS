#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "bcos-task/Task.h"
#include <type_traits>
#include <utility>

namespace bcos::transaction_executor
{

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

inline constexpr struct Execute3Step
{
    /**
     * @brief Executes a transaction in three steps.
     *
     * This function is a friend function of the TransactionExecutorImpl class.
     * It executes a transaction in three steps and returns a generator that yields
     * transaction receipts.
     *
     * 分三个步骤执行交易，可流水线执行
     * Transaction are executed in three steps, which can be pipelined
     *
     * @param executor The reference to the TransactionExecutorImpl object.
     * @param storage The reference to the storage object.
     * @param blockHeader The reference to the block header object.
     * @param transaction The reference to the transaction object.
     * @param contextID The context ID.
     * @param ledgerConfig The reference to the ledger configuration object.
     * @param waitOperator The wait operator.
     *
     * @return A generator that yields transaction receipts.
     */
    auto operator()(auto& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, auto&&... args) const
    {
        return tag_invoke(*this, executor, storage, blockHeader, transaction,
            std::forward<decltype(args)>(args)...);
    }
} execute3Step{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_executor
