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

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey)
    {
        size_t hash = std::hash<bcos::transaction_executor::TableNameID>{}(std::get<0>(stateKey));
        boost::hash_combine(hash, std::hash<std::string>{}(std::get<1>(stateKey)));
        return hash;
    }
};