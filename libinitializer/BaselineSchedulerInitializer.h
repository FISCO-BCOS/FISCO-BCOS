#pragma once
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-framework/storage2/MemoryStorage.h>
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

    std::shared_ptr<protocol::BlockFactory> m_blockFactory;

    transaction_executor::TableNamePool m_tableNamePool;
    storage2::rocksdb::RocksDBStorage2<transaction_executor::StateKey,
        transaction_executor::StateValue, storage2::rocksdb::StateKeyResolver,
        storage2::rocksdb::StateValueResolver>
        m_rocksDBStorage;
    bcos::ledger::LedgerImpl2<Hasher, decltype(m_rocksDBStorage), decltype(*m_blockFactory)>
        m_ledger;
    MultiLayerStorage<MutableStorage, void, decltype(m_rocksDBStorage)> m_multiLayerStorage;

    std::conditional_t<enableParallel,
        SchedulerParallelImpl<decltype(m_multiLayerStorage),
            std::remove_reference_t<decltype(*m_blockFactory->receiptFactory())>,
            transaction_executor::TransactionExecutorImpl>,
        SchedulerSerialImpl<decltype(m_multiLayerStorage),
            std::remove_reference_t<decltype(*m_blockFactory->receiptFactory())>,
            transaction_executor::TransactionExecutorImpl>>
        m_scheduler;

public:
    BaselineSchedulerInitializer(
        ::rocksdb::DB& rocksDB, std::shared_ptr<protocol::BlockFactory> blockFactory)
      : m_blockFactory(std::move(blockFactory)),
        m_rocksDBStorage(rocksDB, storage2::rocksdb::StateKeyResolver{m_tableNamePool},
            storage2::rocksdb::StateValueResolver{}),
        m_ledger(m_rocksDBStorage, *m_blockFactory, m_tableNamePool),
        m_multiLayerStorage(m_rocksDBStorage),
        m_scheduler(m_multiLayerStorage, *m_blockFactory->receiptFactory(), m_tableNamePool)
    {}

    auto buildScheduler()
    {
        auto baselineScheduler = std::make_shared<BaselineScheduler<decltype(m_scheduler),
            decltype(*m_blockFactory->blockHeaderFactory()), decltype(m_ledger)>>(m_scheduler,
            *m_blockFactory->blockHeaderFactory(), m_ledger,
            *m_blockFactory->cryptoSuite()->hashImpl());
        return baselineScheduler;
    }
};
}  // namespace bcos::transaction_scheduler