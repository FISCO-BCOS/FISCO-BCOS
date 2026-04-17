#pragma once
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"

namespace bcos::executor_v1
{
using MutableStorage = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
    storage2::memory_storage::ORDERED>;
}  // namespace bcos::executor_v1