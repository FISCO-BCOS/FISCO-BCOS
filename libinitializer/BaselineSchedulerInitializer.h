#pragma once
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
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

    ::rocksdb::DB& m_rocksDB;
    std::shared_ptr<protocol::BlockHeaderFactory> m_blockHeaderFactory;
    std::shared_ptr<protocol::TransactionReceiptFactory> m_receiptFactory;
    crypto::CryptoSuite::Ptr m_cryptoSuite;

    storage2::rocksdb::RocksDBStorage2<transaction_executor::StateKey,
        transaction_executor::StateValue, storage2::rocksdb::StateKeyResolver,
        storage2::rocksdb::StateValueResolver>
        m_rocksDBStorage;
    bcos::ledger::LedgerImpl2<Hasher, decltype(m_rocksDBStorage), decltype(*m_blockHeaderFactory)>
        m_ledger;
    MultiLayerStorage<MutableStorage, void, decltype(m_rocksDBStorage)> m_multiLayerStorage;

    std::conditional_t<enableParallel,
        SchedulerParallelImpl<decltype(m_multiLayerStorage),
            std::remove_reference_t<decltype(*m_receiptFactory)>,
            transaction_executor::TransactionExecutorImpl>,
        SchedulerSerialImpl<decltype(m_multiLayerStorage),
            std::remove_reference_t<decltype(*m_receiptFactory)>,
            transaction_executor::TransactionExecutorImpl>>
        m_scheduler;

    auto buildScheduler()
    {
        auto baselineScheduler = std::make_shared<BaselineScheduler<decltype(m_scheduler),
            decltype(*m_blockHeaderFactory), decltype(m_ledger)>>(
            m_scheduler, *m_blockHeaderFactory, m_ledger, *m_cryptoSuite->hashImpl());
        return baselineScheduler;
    }


public:
};
}  // namespace bcos::transaction_scheduler