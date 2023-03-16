#pragma once

#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../storage/Entry.h"
#include "../storage2/StringPool.h"
#include <bcos-task/Trait.h>

namespace bcos::transaction_executor
{

using TableNameID = storage2::string_pool::StringPool<>::StringID;
using StateKey = std::tuple<TableNameID, std::string_view>;
using StateValue = storage::Entry;

template <class TransactionExecutorType>
concept TransactionExecutor = requires(
    TransactionExecutorType executor, const protocol::Transaction& transaction)
{
    requires std::same_as<typename task::AwaitableReturnType<decltype(executor.execute(transaction))>,
        protocol::TransactionReceipt>;
};

// All auto interfaces is awaitable
}  // namespace bcos::transaction_executor