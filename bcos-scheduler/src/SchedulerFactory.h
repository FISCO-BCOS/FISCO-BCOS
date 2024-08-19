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
        size_t keyPageSize = 10240)
      : m_executorManager(std::move(executorManager)),
        m_ledger(std::move(ledger)),
        m_storage(std::move(storage)),
        m_executionMessageFactory(std::move(executionMessageFactory)),
        m_blockFactory(std::move(blockFactory)),
        m_txPool(std::move(txPool)),
        m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_isAuthCheck(isAuthCheck),
        m_isWasm(isWasm),
        m_isSerialExecute(isSerialExecute),
        m_keyPageSize(keyPageSize)
    {}

    scheduler::SchedulerImpl::Ptr build(int64_t schedulerTermId)
    {
        auto scheduler = std::make_shared<scheduler::SchedulerImpl>(m_executorManager, m_ledger,
            m_storage, m_executionMessageFactory, m_blockFactory, m_txPool,
            m_transactionSubmitResultFactory, m_hashImpl, m_isAuthCheck, m_isWasm,
            m_isSerialExecute, schedulerTermId, m_keyPageSize);
        scheduler->fetchConfig();

        scheduler->registerBlockNumberReceiver(m_blockNumberReceiver);
        scheduler->registerTransactionNotifier(m_txNotifier);

        return scheduler;
    }

    void setBlockNumberReceiver(std::function<void(protocol::BlockNumber blockNumber)> callback)
    {
        m_blockNumberReceiver = std::move(callback);
    }

    void setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier)
    {
        m_txNotifier = std::move(txNotifier);
    }

    bcos::ledger::LedgerInterface::Ptr getLedger() { return m_ledger; }

    void stop() { m_storage->stop(); }

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
