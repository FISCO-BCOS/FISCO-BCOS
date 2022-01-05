#include "BlockExecutive.h"
#include "ExecutorManager.h"
#include "SchedulerImpl.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "bcos-framework/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/protocol/BlockHeaderFactory.h"
#include "bcos-framework/interfaces/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "mock/MockExecutor.h"
#include "mock/MockExecutor3.h"
#include "mock/MockLedger2.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-framework/libexecutor/NativeExecutionMessage.h>
#include <bcos-tars-protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>

using namespace bcos::storage;
using namespace bcos::ledger;
using namespace std;

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

        scheduler = std::make_shared<scheduler::SchedulerImpl>(
            executorManager, ledger, storage, executionMessageFactory, blockFactory, hashImpl);

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

    std::string path = "./unittestdb";
    RocksDBStorage::Ptr storage = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(BlockExecutive, BlockExecutiveFixture)

BOOST_AUTO_TEST_CASE(commitBlock)
{
    // Add executor
    executorManager->addExecutor("executor1", std::make_shared<MockParallelExecutor3>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t i = 0; i < 10; ++i)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract1");
        block->appendTransactionMetaData(std::move(metaTx));
    }

    for (size_t i = 10; i < 20; ++i)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract2");
        block->appendTransactionMetaData(std::move(metaTx));
    }

    for (size_t i = 20; i < 30; ++i)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract3");
        block->appendTransactionMetaData(std::move(metaTx));
    }

    bcos::protocol::BlockHeader::Ptr executedHeader;

    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
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
    BOOST_CHECK_EQUAL(blockNumber, "100");
}

BOOST_AUTO_TEST_CASE(rollback) {}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test