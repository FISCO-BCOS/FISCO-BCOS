#pragma once
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-storage/bcos-storage/StateKVResolver.h"
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-ledger/src/libledger/LedgerImpl2.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-transaction-executor/TransactionExecutorImpl.h>
#include <bcos-transaction-scheduler/BaselineScheduler.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>

namespace bcos::transaction_scheduler
{

template <bcos::crypto::hasher::Hasher Hasher, bool enableParallel>
class BaselineSchedulerInitializer
{
private:
    using MutableStorage = storage2::memory_storage::MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
    using CacheStorage = storage2::memory_storage::MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::CONCURRENT | storage2::memory_storage::MRU),
        std::hash<bcos::transaction_executor::StateKey>>;

    std::shared_ptr<protocol::BlockFactory> m_blockFactory;
    std::shared_ptr<txpool::TxPoolInterface> m_txpool;
    std::shared_ptr<protocol::TransactionSubmitResultFactory> m_transactionSubmitResultFactory;

    transaction_executor::TableNamePool m_tableNamePool;
    CacheStorage m_cacheStorage;
    storage2::rocksdb::RocksDBStorage2<transaction_executor::StateKey,
        transaction_executor::StateValue, storage2::rocksdb::StateKeyResolver,
        storage2::rocksdb::StateValueResolver>
        m_rocksDBStorage;
    bcos::ledger::LedgerImpl2<decltype(m_rocksDBStorage)> m_ledger;

    MultiLayerStorage<MutableStorage, CacheStorage, decltype(m_rocksDBStorage)> m_multiLayerStorage;

    std::conditional_t<enableParallel,
        SchedulerParallelImpl<decltype(m_multiLayerStorage),
            transaction_executor::TransactionExecutorImpl>,
        SchedulerSerialImpl<decltype(m_multiLayerStorage),
            transaction_executor::TransactionExecutorImpl>>
        m_scheduler;

public:
    BaselineSchedulerInitializer(::rocksdb::DB& rocksDB,
        std::shared_ptr<protocol::BlockFactory> blockFactory,
        std::shared_ptr<txpool::TxPoolInterface> txpool,
        std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory)
      : m_blockFactory(std::move(blockFactory)),
        m_txpool(std::move(txpool)),
        m_transactionSubmitResultFactory(std::move(transactionSubmitResultFactory)),
        m_rocksDBStorage(rocksDB, storage2::rocksdb::StateKeyResolver{m_tableNamePool},
            storage2::rocksdb::StateValueResolver{}),
        m_ledger(m_rocksDBStorage, *m_blockFactory, m_tableNamePool),
        m_multiLayerStorage(m_rocksDBStorage, m_cacheStorage),
        m_scheduler(m_multiLayerStorage, *m_blockFactory->receiptFactory(), m_tableNamePool)
    {}

    auto buildScheduler()
    {
        auto baselineScheduler =
            std::make_shared<BaselineScheduler<decltype(m_scheduler), decltype(m_ledger)>>(
                m_scheduler, *m_blockFactory->blockHeaderFactory(), m_ledger, *m_txpool,
                *m_transactionSubmitResultFactory, *m_blockFactory->cryptoSuite()->hashImpl());
        return baselineScheduler;
    }
};
}  // namespace bcos::transaction_scheduler