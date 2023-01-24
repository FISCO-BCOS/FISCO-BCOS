#pragma once

#include "../storage/Entry.h"
#include "../storage2/Storage.h"
#include "../storage2/StringPool.h"
#include <boost/container_hash/hash_fwd.hpp>
#include <boost/functional/hash.hpp>
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
}  // namespace bcos::transaction_executor