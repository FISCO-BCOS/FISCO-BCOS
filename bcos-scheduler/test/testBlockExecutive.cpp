#include "ExecutorManager.h"
#include "SchedulerImpl.h"
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "mock/MockExecutor.h"
#include "mock/MockExecutor3.h"
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
struct BlockExecutiveFixture
{
    BlockExecutiveFixture()
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

        scheduler = std::make_shared<scheduler::SchedulerImpl>(executorManager, ledger, storage,
            executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory, hashImpl,
            false, false, false, 1);

        blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
            block, scheduler, 1, TransactionSubmitResultFactory, false, blockFactory, txPool);

        std::promise<std::optional<Table>> createTablePromise;
        storage->asyncCreateTable(SYS_CURRENT_STATE, "value",
            [&createTablePromise](auto&& error, std::optional<Table>&& table) {
                BOOST_CHECK_EQUAL(error.get(), nullptr);
                createTablePromise.set_value(table);
            });
        auto createTableResult = createTablePromise.get_future().get();
        BOOST_CHECK_EQUAL(createTableResult.has_value(), true);
    }

    ~BlockExecutiveFixture()
    {
        filesystem::path p(path);

        if (filesystem::exists(p))
        {
            filesystem::remove_all(p);
        }
    }

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
    bcos::scheduler::BlockExecutive::Ptr blockExecutive;

    std::string path = "./unittestdb";
    RocksDBStorage::Ptr storage = nullptr;
};
BOOST_FIXTURE_TEST_SUITE(TestBlockExecutive, BlockExecutiveFixture)
BOOST_AUTO_TEST_CASE()
BOOST_AUTO_TEST_CASE_END()
}  // namespace bcos::test