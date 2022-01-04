#pragma once

#include "interfaces/storage/StorageInterface.h"
#include <bcos-executor/src/executor/TransactionExecutor.h>

namespace bcos::initializer
{
class ExecutorInitializer
{
public:
    static bcos::executor::TransactionExecutor::Ptr build(txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cache,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck)
    {
        return std::make_shared<executor::TransactionExecutor>(
            txpool, cache, storage, executionMessageFactory, hashImpl, isWasm, isAuthCheck);
    }
};
}  // namespace bcos::initializer