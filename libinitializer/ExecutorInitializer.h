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
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, std::string name)
    {
        return bcos::executor::TransactionExecutorFactory::build(ledger, txpool, cache, storage,
            executionMessageFactory, hashImpl, isWasm, isAuthCheck, name);
    }

    static bcos::executor::TransactionExecutorFactory::Ptr buildFactory(
        bcos::ledger::LedgerInterface::Ptr ledger, txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cache,

        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, std::string name)
    {
        return std::make_shared<bcos::executor::TransactionExecutorFactory>(ledger, txpool, cache,
            storage, executionMessageFactory, hashImpl, isWasm, isAuthCheck, name);
    }

    static bcos::executor::TransactionExecutorFactory::Ptr buildFactory(
        bcos::ledger::LedgerInterface::Ptr ledger, txpool::TxPoolInterface::Ptr txpool,
        storage::MergeableStorageInterface::Ptr cache,

        storage::TransactionalStorageInterface::Ptr storage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck)
    {
        return std::make_shared<bcos::executor::TransactionExecutorFactory>(
            ledger, txpool, cache, storage, executionMessageFactory, hashImpl, isWasm, isAuthCheck);
    }
};
}  // namespace bcos::initializer