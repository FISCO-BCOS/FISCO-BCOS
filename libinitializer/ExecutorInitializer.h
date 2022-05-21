#pragma once

#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include <bcos-executor/src/executor/TransactionExecutorFactory.h>

namespace bcos::initializer
{
class ExecutorInitializer
{
public:
    static bcos::executor::ParallelTransactionExecutorInterface::Ptr build(
        txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cache,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize = 0)
    {
        return bcos::executor::TransactionExecutorFactory::build(
            txpool, cache, storage, executionMessageFactory, hashImpl, isWasm, isAuthCheck, keyPageSize);
    }
};
}  // namespace bcos::initializer