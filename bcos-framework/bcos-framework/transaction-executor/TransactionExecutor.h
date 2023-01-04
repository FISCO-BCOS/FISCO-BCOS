#pragma once

#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include <bcos-task/Trait.h>

namespace bcos::transaction_executor
{

template <class TransactionExecutorType>
concept TransactionExecutor = requires(
    TransactionExecutorType executor, const protocol::Transaction& transaction)
{
    std::same_as<typename task::AwaitableReturnType<decltype(executor.execute(transaction))>,
        protocol::TransactionReceipt>;
};

// All auto interfaces is awaitable
}  // namespace bcos::transaction_executor