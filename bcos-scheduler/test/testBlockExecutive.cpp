#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutiveFactory.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-storage/src/RocksDBStorage.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "mock/MockBlockExecutive.h"
#include "mock/MockBlockExecutiveFactory.h"
#include "mock/MockDmcExecutor.h"
#include "mock/MockExecutor.h"
#include "mock/MockExecutorForCall.h"
#include "mock/MockExecutorForCreate.h"
#include "mock/MockLedger3.h"
#include "mock/MockTxpool1.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-protocol/testutils/protocol/FakeTransaction.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Error.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <tbb/parallel_for.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>
#include <optional>


using namespace std;
using namespace bcos;
using namespace rocksdb;
using namespace bcos::ledger;
using namespace bcos::storage;
using namespace bcos::scheduler;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::txpool;
using namespace bcos::test;
using namespace bcostars::protocol;
namespace bcos::test
{
struct BlockExecutiveFixture
{
    BlockExecutiveFixture()
    {
        hashImpl = std::make_shared<Keccak256>();
        signature = std::make_shared<Secp256k1Crypto>();
        suite = std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signature, nullptr);
        ledger = std::make_shared<MockLedger3>();
        executorManager = std::make_shared<scheduler::ExecutorManager>();

        // create RocksDBStorage
        rocksdb::DB* db;
        rocksdb::Options options;
        options.create_if_missing = true;
        std::string path = "./unittestdb";
        // open DB
        rocksdb::Status s = rocksdb::DB::Open(options, path, &db);
        storage = std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);

        transactionFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite);
        transactionReceiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
        blockHeaderFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite);
        executionMessageFactory = std::make_shared<bcos::executor::NativeExecutionMessageFactory>();

        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            suite, blockHeaderFactory, transactionFactory, transactionReceiptFactory);
        keyPair = blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
        txPool = std::make_shared<MockTxPool1>();
        transactionSubmitResultFactory =
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>();
        scheduler = std::make_shared<bcos::scheduler::SchedulerImpl>(executorManager, ledger,
            storage, executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
            hashImpl, false, false, false, 0);
    }

    ~BlockExecutiveFixture() {}

    bcos::ledger::LedgerInterface::Ptr ledger;
    bcos::scheduler::ExecutorManager::Ptr executorManager;
    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr transactionReceiptFactory;
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::scheduler::SchedulerImpl::Ptr scheduler;
    bcostars::protocol::TransactionFactoryImpl::Ptr transactionFactory;
    bcos::protocol::Block::Ptr block;
    bcos::crypto::SignatureCrypto::Ptr signature;
    bcos::crypto::CryptoSuite::Ptr suite;
    bcos::protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<MockTxPool1> txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory;
    bcos::scheduler::BlockExecutiveFactory::Ptr blockExecutiveFactory;
    bcos::scheduler::BlockExecutive::Ptr blockExecutive;
    bcos::crypto::KeyPairInterface::Ptr keyPair;
    bcos::storage::RocksDBStorage::Ptr storage = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(testBlockExecutive, BlockExecutiveFixture)
BOOST_AUTO_TEST_CASE(prepareTest)
{
    SCHEDULER_LOG(DEBUG) << "----------prepareTest----------------";
    // Generate Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(9999);

    // Add Executor
    for (size_t i = 1; i <= 10; ++i)
    {
        auto executor =
            std::make_shared<MockDmcExecutor>("executor" + boost::lexical_cast<std::string>(i));
        executorManager->addExecutor("executor" + boost::lexical_cast<std::string>(i), executor);
    }

    // Generate MetaTx
    for (size_t j = 0; j < 50; ++j)
    {
        // Fill txPool
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        auto hash = tx->hash();
        SCHEDULER_LOG(DEBUG) << LOG_KV("hash", hash);
        txPool->hash2Transaction.emplace(hash, tx);
        block->appendTransaction(std::move(tx));
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            hash, "contract" + boost::lexical_cast<std::string>((j + 1) % 10));
        block->appendTransactionMetaData(std::move(metaTx));
    }
    SCHEDULER_LOG(DEBUG) << LOG_KV("metaTx size:", block->transactionsMetaDataSize())
                         << LOG_KV("transaction size", block->transactionsSize());

    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(block, scheduler.get(),
        0, transactionSubmitResultFactory, false, blockFactory, txPool, 3000000000, false);
    blockExecutive->prepare();
    // BOOST_CHECK();
}

BOOST_AUTO_TEST_CASE(asyncExecuteTest1)
{
    SCHEDULER_LOG(DEBUG) << "----------asyncExecuteTest----------------";
    // Generate Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(99);
    // Add Executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    // Fill normalTx
    for (size_t i = 0; i < 10; i++)
    {
        std::string inputStr = "hello world!";
        bytes input(inputStr.begin(), inputStr.end());
        auto tx = transactionFactory->createTransaction(20,
            "contract" + boost::lexical_cast<std::string>((i + 1) % 10), input, i, 200, "chainID",
            "groupID", 400, keyPair);
        // tx->setAttribute(bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransaction(tx);
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);
    SCHEDULER_LOG(DEBUG) << LOG_KV("blockExecutive", blockExecutive);
    blockExecutive->asyncExecute([&](Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                                     bool) { BOOST_CHECK(!error); });
}
BOOST_AUTO_TEST_CASE(asyncExecuteTest2)
{
    SCHEDULER_LOG(DEBUG) << "----------asyncExecuteTest----------------";
    // Generate Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(99);
    // Add Executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    // Fill metaTx
    for (size_t j = 0; j < 10; j++)
    {
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        auto hash = tx->hash();
        txPool->hash2Transaction.emplace(hash, tx);
        block->appendTransaction(std::move(tx));
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            hash, "contract" + boost::lexical_cast<std::string>((j + 1) % 10));
        block->appendTransactionMetaData(std::move(metaTx));
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);
    SCHEDULER_LOG(DEBUG) << LOG_KV("blockExecutive", blockExecutive);
    blockExecutive->asyncExecute([&](Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                                     bool) { BOOST_CHECK(!error); });
}

BOOST_AUTO_TEST_CASE(asyncCommitTest1)
{
    SCHEDULER_LOG(DEBUG) << "----------asyncCommitTest1----------------";
    // Generate Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(999);
    // Add Executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);
    // Fill MetaTx
    for (size_t j = 0; j < 10; j++)
    {
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        auto hash = tx->hash();
        txPool->hash2Transaction.emplace(hash, tx);
        block->appendTransaction(std::move(tx));
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            hash, "contract" + boost::lexical_cast<std::string>((j + 1) % 10));
        block->appendTransactionMetaData(std::move(metaTx));
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);
    SCHEDULER_LOG(DEBUG) << LOG_KV("blockExecutive", blockExecutive);
    blockExecutive->asyncCommit([&](Error::UniquePtr error) { BOOST_CHECK(!error); });
}

BOOST_AUTO_TEST_CASE(asyncCommitTest2)
{
    SCHEDULER_LOG(DEBUG) << "----------asyncCommitTest2----------------";
    // Generate Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(999);
    // Add Executor
    for (size_t i = 1; i <= 10; ++i)
    {
        auto executor =
            std::make_shared<MockDmcExecutor>("executor" + boost::lexical_cast<std::string>(i));
        executorManager->addExecutor("executor" + boost::lexical_cast<std::string>(i), executor);
    }
    // Fill normalTransaction
    for (size_t j = 0; j < 100; j++)
    {
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        auto hash = tx->hash();
        txPool->hash2Transaction.emplace(hash, tx);
        block->appendTransaction(std::move(tx));
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);
    SCHEDULER_LOG(DEBUG) << LOG_KV("blockExecutive", blockExecutive);
    blockExecutive->asyncCommit([&](Error::UniquePtr error) {
        BOOST_CHECK(!error);
        SCHEDULER_LOG(DEBUG) << "----------asyncCommitTest2  END----------------";
    });
}


BOOST_AUTO_TEST_CASE(asyncNotify)
{
    // Generate block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(999);
    // Add Executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    // Fill metaTX
    for (size_t j = 0; j < 20; ++j)
    {
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        // auto hash = tx->hash();
        // txPool->hash2Transaction.emplace(hash, tx);
        // auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        //     hash, "contract" + boost::lexical_cast<std::string>((j + 1) % 10));
        block->appendTransaction(std::move(tx));
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);

    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_txNotifier;

    bcos::protocol::BlockNumber blockNumber = 999;
    m_txNotifier = [&](bcos::protocol::BlockNumber blockNumber,
                       bcos::protocol::TransactionSubmitResultsPtr,
                       std::function<void(Error::Ptr)> _callback) {

    };
    blockExecutive->asyncNotify(
        m_txNotifier, [&](Error::Ptr _error) mutable { BOOST_CHECK(!_error); });
}

BOOST_AUTO_TEST_CASE(dagTest)
{
    // Add executor
    executorManager->addExecutor("executor1", std::make_shared<MockDmcExecutor>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t j = 0; j < 10; ++j)
    {
        std::string inputStr = "Hello world! request";
        auto tx = blockFactory->transactionFactory()->createTransaction(0,
            "contract" + boost::lexical_cast<std::string>((j + 1) % 10),
            bytes(inputStr.begin(), inputStr.end()), j, 300, "chain", "group", 500, keyPair);
        auto hash = tx->hash();
        txPool->hash2Transaction.emplace(hash, tx);
        block->appendTransaction(std::move(tx));
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            hash, "contract" + boost::lexical_cast<std::string>((j + 1) % 10));
        metaTx->setAttribute(bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransactionMetaData(std::move(metaTx));
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);

    blockExecutive->asyncExecute(
        [this](Error::UniquePtr error, protocol::BlockHeader::Ptr header, bool) {
            BOOST_CHECK(!error);
            SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
            SCHEDULER_LOG(DEBUG) << "----------dagTest  END----------------";
        });
}

BOOST_AUTO_TEST_CASE(dagByMessage)
{
    SCHEDULER_LOG(DEBUG) << "----------dagByMessageTest----------------";
    // Add executor
    executorManager->addExecutor("executor1", std::make_shared<MockDmcExecutor>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t i = 0; i < 10; ++i)
    {
        std::string inputStr = "hello world!";
        bytes input(inputStr.begin(), inputStr.end());
        auto tx = transactionFactory->createTransaction(20,
            "contract" + boost::lexical_cast<std::string>((i + 1) % 10), input, i, 200, "chainID",
            "groupID", 400, keyPair);
        tx->setAttribute(bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransaction(tx);
    }
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(
        block, scheduler.get(), 0, transactionSubmitResultFactory, false, blockFactory, txPool);
    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    blockExecutive->asyncExecute([this](Error::UniquePtr error, protocol::BlockHeader::Ptr header,
                                     bool) { BOOST_CHECK(!error); });
}

BOOST_AUTO_TEST_CASE(callTest)
{
    SCHEDULER_LOG(DEBUG) << "----------callTest----------------";
    // Generate block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(999);

    // Generate call transaction
    std::string inputStr = "Hello world! request";
    bcos::crypto::KeyPairInterface::Ptr keyPair =
        blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
    auto tx = blockFactory->transactionFactory()->createTransaction(0, "address_to",
        bytes(inputStr.begin(), inputStr.end()), 200, 300, "chain", "group", 500, keyPair);
    block->appendTransaction(std::move(tx));
    // Add executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    // Build blockExecutive
    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(block, scheduler.get(),
        0, transactionSubmitResultFactory, false, blockFactory, txPool, 3000000000, false);
    // call
    {
        bcos::protocol::TransactionReceipt::Ptr receipt;
        blockExecutive->asyncCall([&](bcos::Error::Ptr&& error,
                                      bcos::protocol::TransactionReceipt::Ptr&& receiptResponse) {
            if (error)
            {
                BOOST_CHECK(error);
                return;
            }
            BOOST_CHECK(!error);
            BOOST_CHECK(receiptResponse);
            receipt = std::move(receiptResponse);
        });
        // BOOST_CHECK_EQUAL(receipt->blockNumber(), 0);
        // BOOST_CHECK_EQUAL(receipt->status(), 0);
        // BOOST_CHECK_GT(receipt->gasUsed(), 0);
        // auto output = receipt->output();
        // std::string outputStr((char*)output.data(), output.size());
        // SCHEDULER_LOG(DEBUG) << LOG_KV("outputStr", outputStr);
        // BOOST_CHECK_EQUAL(outputStr, "Hello world! response");
    }
}
BOOST_AUTO_TEST_CASE(executeWithSystemError)
{
    SCHEDULER_LOG(DEBUG) << "----------executeWithSystemError----------------";
    // Generate block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    auto tx = blockFactory->transactionFactory()->createTransaction(
        3, "0xaabbccdd", {}, u256(1), 500, "chainId", "groupId", utcTime());
    block->appendTransaction(std::move(tx));

    // Add Executor
    auto executor1 = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    auto blockExecutive = std::make_shared<bcos::scheduler::BlockExecutive>(block, scheduler.get(),
        0, transactionSubmitResultFactory, false, blockFactory, txPool, 3000000000, false);

    bool errorFlag = false;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    blockExecutive->asyncExecute(
        [&](Error::UniquePtr error, protocol::BlockHeader::Ptr header, bool) {
            if (error)
            {
                SCHEDULER_LOG(DEBUG) << "I am executeWithSystemError";
                BOOST_CHECK(error);
                errorFlag = true;
                return;
            }
            else
            {
                executedHeader = std::move(header);
            }
        });
    BOOST_CHECK(errorFlag);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test