#pragma once

#include "../storage/Entry.h"
#include "../storage2/Storage.h"
#include "../storage2/StringPool.h"

namespace bcos::transaction_executor
{

using TableNamePool = storage2::string_pool::StringPool<>;
using TableNameID = storage2::string_pool::StringPool<>::StringID;
using StateKey = std::tuple<TableNameID, std::string>;
using StateValue = storage::Entry;

template <class StorageType>
concept StateStorage =
    storage2::ReadableStorage<StorageType, StateKey> && storage2::WriteableStorage<StorageType, StateKey, StateValue>;
}  // namespace bcos::transaction_executor