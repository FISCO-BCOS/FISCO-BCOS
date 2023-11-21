#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../protocol/TransactionReceiptFactory.h"
#include "../storage2/Storage.h"
#include "StateKey.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Trait.h>
#include <compare>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::transaction_executor
{

struct ExecuteTransaction
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
        const protocol::Transaction& transaction, auto&&... args) const
        -> task::Task<protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke(*this, executor, storage, blockHeader, transaction,
            std::forward<decltype(args)>(args)...);
    }
};
inline constexpr ExecuteTransaction executeTransaction{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_executor
