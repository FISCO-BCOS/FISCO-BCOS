#pragma once

#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include <bcos-executor/src/executor/TransactionExecutorFactory.h>

namespace bcos::initializer
{
class ExecutorInitializer
{
public:
    static bcos::executor::ParallelTransactionExecutorInterface::Ptr build(
        bcos::ledger::LedgerInterface::Ptr ledger, txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cache,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize,
        std::string name)
    {
        return bcos::executor::TransactionExecutorFactory::build(ledger, txpool, cache, storage,
            executionMessageFactory, hashImpl, isWasm, isAuthCheck, keyPageSize, name);
    }

    static bcos::executor::TransactionExecutorFactory::Ptr buildFactory(
        bcos::ledger::LedgerInterface::Ptr ledger, txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cache,
        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize = 0,
        std::string name = "executor")
    {
        return std::make_shared<bcos::executor::TransactionExecutorFactory>(ledger, txpool, cache,
            storage, executionMessageFactory, hashImpl, isWasm, isAuthCheck, keyPageSize, name);
    }
};
}  // namespace bcos::initializer