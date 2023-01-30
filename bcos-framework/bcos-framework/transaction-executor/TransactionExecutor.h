#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../storage/Entry.h"
#include "../storage2/StringPool.h"
#include <bcos-task/Trait.h>

namespace bcos::transaction_executor
{
using TableNamePool = storage2::string_pool::FixedStringPool<>;
using TableNameID = storage2::string_pool::StringID;
using StateKey = std::tuple<TableNameID, std::string>;
using StateValue = storage::Entry;

constexpr std::string_view EVM_CONTRACT_PREFIX("/apps/");

template <class StorageType>
concept StateStorage = storage2::ReadableStorage<StorageType, StateKey> &&
    storage2::WriteableStorage<StorageType, StateKey, StateValue>;

template <class TransactionExecutorType>
concept TransactionExecutor = requires(TransactionExecutorType executor,
    const protocol::BlockHeader& blockHeader, const protocol::Transaction& transaction)
{
    requires protocol::IsTransactionReceipt<typename task::AwaitableReturnType<
        decltype(executor.execute(blockHeader, transaction, 0))>>;
};

// All auto interfaces is awaitable
}  // namespace bcos::transaction_executor