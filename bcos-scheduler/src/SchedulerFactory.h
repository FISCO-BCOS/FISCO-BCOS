#pragma once
#include <utility>
#include "SchedulerImpl.h"

namespace bcos::scheduler
{
class SchedulerFactory
{
public:
    using Ptr = std::shared_ptr<SchedulerFactory>;

    SchedulerFactory(ExecutorManager::Ptr executorManager,
        bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::storage::TransactionalStorageInterface::Ptr storage,
        bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, bool isSerialExecute,
        size_t keyPageSize = 10240);

    scheduler::SchedulerImpl::Ptr build(int64_t schedulerTermId);

    void setBlockNumberReceiver(std::function<void(protocol::BlockNumber blockNumber)> callback);

    void setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier);

    bcos::ledger::LedgerInterface::Ptr getLedger();

    void stop();

private:
    ExecutorManager::Ptr m_executorManager;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage;
    bcos::protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::txpool::TxPoolInterface::Ptr m_txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_transactionSubmitResultFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    bool m_isAuthCheck;
    bool m_isWasm;
    bool m_isSerialExecute;
    size_t m_keyPageSize;

    std::function<void(protocol::BlockNumber blockNumber)> m_blockNumberReceiver;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_txNotifier;
};

}  // namespace bcos::scheduler
