#include "SchedulerFactory.h"

using namespace bcos::scheduler;

SchedulerFactory::SchedulerFactory(ExecutorManager::Ptr executorManager,
    bcos::ledger::LedgerInterface::Ptr ledger,
    bcos::storage::TransactionalStorageInterface::Ptr storage,
    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::txpool::TxPoolInterface::Ptr txPool,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isAuthCheck, bool isWasm, bool isSerialExecute,
    size_t keyPageSize)
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

scheduler::SchedulerImpl::Ptr SchedulerFactory::build(int64_t schedulerTermId)
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

void SchedulerFactory::setBlockNumberReceiver(
    std::function<void(protocol::BlockNumber blockNumber)> callback)
{
    m_blockNumberReceiver = std::move(callback);
}

void SchedulerFactory::setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)> txNotifier)
{
    m_txNotifier = std::move(txNotifier);
}

bcos::ledger::LedgerInterface::Ptr SchedulerFactory::getLedger()
{
    return m_ledger;
}

void SchedulerFactory::stop()
{
    m_storage->stop();
}