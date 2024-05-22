#pragma once
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"

namespace bcos::transaction_executor
{
using MutableStorage = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
    storage2::memory_storage::ORDERED>;

auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, MutableStorage& storage,
    RANGES::input_range auto&& keys, storage2::DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readSome(storage, keys))>>
{
    co_return co_await storage2::readSome(storage, std::forward<decltype(keys)>(keys));
}

auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, MutableStorage& storage,
    const auto& key, storage2::DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readOne(storage, key))>>
{
    co_return co_await storage2::readOne(storage, key);
}
}  // namespace bcos::transaction_executor