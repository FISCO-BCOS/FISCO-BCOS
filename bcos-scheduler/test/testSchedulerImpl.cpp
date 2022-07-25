#include "ExecutorManager.h"
#include "SchedulerImpl.h"
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "mock/MockDmcExecutor.h"
#include "mock/MockLedger2.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-tars-protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>

using namespace std;
using namespace bcos;
using namespace bcos::scheduler;
using namespace bcos::crypto;

namespace bcos::test
{
struct schedulerImplFixture
{
    schedulerImplFixture()
    {
        hashImpl = std::make_shared<Keccak256>();
        signature = std::make_shared<Secp256k1Crypto>();
        suite = std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signature, nullptr);
        ledger = std::make_shared<MockLedger2>();
        executorManager = std::make_shared<scheduler::ExecutorManager>();

        // create RocksDBStorage
        rocksdb::DB* db;
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::Status s = rocksdb::DB::Open(options, path, &db);
        BOOST_CHECK_EQUAL(s.ok(), true);
        storage = std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db));

        transactionFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite);
        transactionReceiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
        blockHeaderFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite);
        executionMessageFactory = std::make_shared<bcos::executor::NativeExecutionMessageFactory>();

        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            suite, blockHeaderFactory, transactionFactory, transactionReceiptFactory);

        txPool = std::make_shared<MockTxPool>();
        transactionSubmitResultFactory =
            std::make_shared<bcos::protocol::TransactionSubmitResultFactory>();

        // scheduler = std::make_shared<scheduler::SchedulerImpl>(executorManager, ledger, storage,
        //     executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
        //     hashImpl, false, false, false, 0);
    };
    ~schedulerImplFixture() {}
    ledger::LedgerInterface::Ptr ledger;
    scheduler::ExecutorManager::Ptr executorManager;
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory;
    protocol::TransactionReceiptFactory::Ptr transactionReceiptFactory;
    protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
    bcos::crypto::Hash::Ptr hashImpl;
    scheduler::SchedulerImpl::Ptr scheduler;

    bcostars::protocol::TransactionFactoryImpl::Ptr transactionFactory;
    bcos::crypto::SignatureCrypto::Ptr signature;
    bcos::crypto::CryptoSuite::Ptr suite;
    bcostars::protocol::BlockFactoryImpl::Ptr blockFactory;
    bcos::txpool::TxPoolInterface::Ptr txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory;

    std::string path = "./unittestdb";
    RocksDBStorage::Ptr storage = nullptr;
};
BOOST_FIXTURE_TEST_SUITE(TestSchedulerImpl, schedulerImplFixture)
BOOST_AUTO_TEST_CASE(executeBlock)
{
    executorManager->addExecutor("executor1", std::make_shared<test::MockDmcExecutor>("executor1"));
    auto scheduler = std::make_shared<scheduler::SchedulerImpl>(executorManager, ledger, storage,
        executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory, hashImpl,
        false, false, false, 0);
    auto block = blockFactory->createBlock();
    block->BlockHeader()->setNumber(95);
    for (size_t i = 0; i < 20; ++i)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract1");
        block->appendTransactionMetaData(std::move(metaTx));
    }
    bcos::protocol::BlockHeader::Ptr executedHeader;

    scheduler->executeBlock(block, false,
        [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header, bool) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);
            executedHeader = std::move(header);
        });
    scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
            BOOST_CHECK(!error);
            // BOOST_CHECK(config);
            (void)config;
        });
    promise<Table> prom;
    storage->asyncOpenTable(SYS_CURRENT_STATE, [&](auto&& error, auto&& table) {
        BOOST_CHECK_EQUAL(error.get(), nullptr);
        if (table)
        {
            prom.set_value(table.value());
        }
    });
    auto table = prom.get_future().get();
    auto entry = table.getRow(SYS_KEY_CURRENT_NUMBER);
    BOOST_CHECK_EQUAL(entry.has_value(), true);
    auto blockNumber = entry->getField("value");
    BOOST_CHECK_EQUAL(blockNumber, "95");
}

BOOST_AUTO_TEST_CASE() {}
BOOST_AUTO_TEST_CASE_END()
}  // namespace bcos::test