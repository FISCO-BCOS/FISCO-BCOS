#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
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
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Error.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
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
using namespace bcostars::protocol;

namespace bcos::test
{
struct schedulerImplFixture
{
    schedulerImplFixture()
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

        txPool = std::make_shared<MockTxPool>();
        transactionSubmitResultFactory =
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>();

        auto scheduler = std::make_shared<bcos::scheduler::SchedulerImpl>(executorManager, ledger,
            storage, executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
            hashImpl, false, false, false, 0);
        auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
        scheduler->setBlockExecutiveFactory(blockExecutiveFactory);
    };

    ~schedulerImplFixture() {}
    bcos::test::MockLedger3::Ptr ledger;
    bcos::scheduler::ExecutorManager::Ptr executorManager;
    bcos::protocol::ExecutionMessageFactory::Ptr executionMessageFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr transactionReceiptFactory;
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::scheduler::SchedulerImpl::Ptr scheduler;
    bcostars::protocol::TransactionFactoryImpl::Ptr transactionFactory;
    bcos::crypto::SignatureCrypto::Ptr signature;
    bcos::crypto::CryptoSuite::Ptr suite;
    bcostars::protocol::BlockFactoryImpl::Ptr blockFactory;
    bcos::txpool::TxPoolInterface::Ptr txPool;
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory;
    bcos::scheduler::BlockExecutiveFactory::Ptr blockExecutiveFactory;
    std::string path = "./unittestdb";
    bcos::storage::RocksDBStorage::Ptr storage = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(testSchedulerImpl, schedulerImplFixture)

BOOST_AUTO_TEST_CASE(executeBlockTest)
{
    auto scheduler = std::make_shared<bcos::scheduler::SchedulerImpl>(executorManager, ledger,
        storage, executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
        hashImpl, false, false, false, 0);
    auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
    scheduler->setBlockExecutiveFactory(blockExecutiveFactory);
    bool executeBlockError = false;

    // execute BlockNumber=7 (storage = 5) block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(7);
    for (size_t j = 0; j < 10; ++j)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract1");
        block->appendTransactionMetaData(std::move(metaTx));
    }
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());

    // executeBlock
    bcos::protocol::BlockHeader::Ptr blockHeader;

    SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHash", block)
                         << LOG_KV("BlockHeaderNumber", block->blockHeader()->number());

    std::promise<bcos::protocol::BlockHeader::Ptr> future;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            if (error)
            {
                executeBlockError = true;
                BOOST_TEST(error);
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_TEST(!error);
                BOOST_TEST(header);
            }
            future.set_value(std::move(header));
        });
    blockHeader = future.get_future().get();
    BOOST_TEST(executeBlockError);
    executeBlockError = false;

    // first time executeBlock, add m_block cache
    for (size_t i = 5; i < 10; ++i)
    {
        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(i);
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i);
        for (size_t j = 0; j < 10; ++j)
        {
            auto metaTx =
                std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract1");
            block->appendTransactionMetaData(std::move(metaTx));
        }
        block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
        // executeBlock
        bcos::protocol::BlockHeader::Ptr blockHeader;
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHash", block)
                             << LOG_KV("BlockHeaderNumber", block->blockHeader()->number());
        std::promise<bcos::protocol::BlockHeader::Ptr> future1;
        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
                if (error)
                {
                    BOOST_TEST(error);
                    executeBlockError = true;
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_TEST(!error);
                    BOOST_TEST(header);
                }
                future1.set_value(std::move(header));
            });
        blockHeader = future1.get_future().get();
        if (!executeBlockError)
        {
            BOOST_TEST(blockHeader);
        }
        executeBlockError = false;
        blockHeader = nullptr;
    }
    // secondTime executeBlock
    for (size_t i = 5; i < 8; i++)
    {
        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(i);
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i);
        for (size_t j = 0; j < 10; ++j)
        {
            auto metaTx =
                std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract2");
            block->appendTransactionMetaData(std::move(metaTx));
        }
        block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
        bcos::protocol::BlockHeader::Ptr executeHeader1;
        // execute olderBlock whenQueueFront whenInQueue
        std::promise<bcos::protocol::BlockHeader::Ptr> future1;
        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                if (error)
                {
                    executeBlockError = true;
                    BOOST_TEST(error);
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_TEST(!error);
                    BOOST_TEST(header);
                }
                future1.set_value(std::move(header));
            });
        executeHeader1 = future1.get_future().get();
        if (!executeBlockError)
        {
            BOOST_TEST(executeHeader1);
        }
        executeBlockError = false;
        executeHeader1 = nullptr;
    }

    auto block11 = blockFactory->createBlock();
    block11->blockHeader()->setNumber(11);
    for (size_t j = 0; j < 10; ++j)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract2");
        block11->appendTransactionMetaData(std::move(metaTx));
    }
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
    bcos::protocol::BlockHeader::Ptr executeHeader11;
    // requestBlock = backNumber + 1
    std::promise<bcos::protocol::BlockHeader::Ptr> future1;
    scheduler->executeBlock(block11, false,
        [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
            if (error)
            {
                BOOST_TEST(error);
                executeBlockError = true;
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_TEST(!error);
                BOOST_TEST(header);
            }
            future1.set_value(std::move(header));
        });
    executeHeader11 = future1.get_future().get();
    BOOST_TEST(executeBlockError);
}
BOOST_AUTO_TEST_CASE(commitBlock)
{
    auto scheduler = std::make_shared<bcos::scheduler::SchedulerImpl>(executorManager, ledger,
        storage, executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
        hashImpl, false, false, false, 0);
    auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
    scheduler->setBlockExecutiveFactory(blockExecutiveFactory);

    // executeBlock, Fill m_block
    bool executeBlockError = false;
    for (size_t i = 5; i < 10; ++i)
    {
        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(i);
        for (size_t j = 0; j < 20; ++j)
        {
            auto metaTx =
                std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract2");
            block->appendTransactionMetaData(std::move(metaTx));
        }
        block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
        // executeBlock
        bcos::protocol::BlockHeader::Ptr blockHeader;
        std::promise<bcos::protocol::BlockHeader::Ptr> future;
        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
                if (error)
                {
                    executeBlockError = true;
                    BOOST_TEST(error);
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_TEST(!error);
                    BOOST_TEST(header);
                }
                future.set_value(std::move(header));
            });
        blockHeader = future.get_future().get();
        if (!executeBlockError)
        {
            BOOST_TEST(blockHeader);
        }
        executeBlockError = false;
        blockHeader = nullptr;
    }
    // commit
    bool commitBlockError = false;
    size_t errorNumber = 0;
    size_t queueFrontNumber = 0;
    ledger->commitSuccess(true);
    ledger->commitSuccess(true);  // the committed block number is 7
    for (size_t i = 7; i < 11; ++i)
    {
        auto blockHeader = blockHeaderFactory->createBlockHeader();
        blockHeader->setNumber(i);
        blockHeader->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
        std::promise<bool> committedPromise;
        scheduler->commitBlock(
            blockHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "CommitBlock error";
                    commitBlockError = true;
                    ++errorNumber;
                    committedPromise.set_value(false);
                }
                else
                {
                    ++queueFrontNumber;
                    BOOST_TEST(config);
                    BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
                    BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
                    BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
                    BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
                    BOOST_CHECK_EQUAL(config->hash().hex(), h256(6).hex());
                    committedPromise.set_value(true);
                }
            });
        if (commitBlockError)
        {
            commitBlockError = false;
        }
    }
    BOOST_CHECK_EQUAL(errorNumber, 4);
    BOOST_CHECK_EQUAL(queueFrontNumber, 0);
    BOOST_TEST(!commitBlockError);
    SCHEDULER_LOG(DEBUG) << LOG_KV("errorNumber", errorNumber)
                         << LOG_KV("queueFrontNumber", queueFrontNumber);

    // commit blockNumber <= 5
    auto blockHeader0 = blockHeaderFactory->createBlockHeader();
    blockHeader0->setNumber(0);
    blockHeader0->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
    std::promise<bool> committedPromise;
    scheduler->commitBlock(
        blockHeader0, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "CommitBlock error";
                commitBlockError = true;
                ++errorNumber;
                committedPromise.set_value(false);
            }
            else
            {
                ++queueFrontNumber;
                BOOST_TEST(config);
                BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
                BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
                BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
                BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
                BOOST_CHECK_EQUAL(config->hash().hex(), h256(5).hex());
                committedPromise.set_value(true);
            }
        });
    SCHEDULER_LOG(DEBUG) << LOG_KV("errorNumber", errorNumber)
                         << LOG_KV("queueFrontNumber", queueFrontNumber);
}

BOOST_AUTO_TEST_CASE(handlerBlockTest)
{
    auto scheduler =
        std::make_shared<SchedulerImpl>(executorManager, ledger, storage, executionMessageFactory,
            blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false, 0);
    auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
    scheduler->setBlockExecutiveFactory(blockExecutiveFactory);

    // create Block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(6);
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());

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
    // preExecuteBlock
    scheduler->preExecuteBlock(
        block, false, [&](bcos::Error::Ptr&& error) { BOOST_TEST(!error); });


    // executeBlock
    bool executeBlockError = false;

    std::promise<bcos::protocol::BlockHeader::Ptr> future;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            if (error)
            {
                executeBlockError = true;
                BOOST_TEST(error);
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_TEST(!error);
                BOOST_TEST(header);
            }
            future.set_value(std::move(header));
        });
    bcos::protocol::BlockHeader::Ptr blockHeader = future.get_future().get();
    BOOST_TEST(blockHeader);


    // commitBlock
    bool commitBlockError = false;
    scheduler->commitBlock(
        blockHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
            if (error)
            {
                SCHEDULER_LOG(ERROR) << "CommitBlock error";
                commitBlockError = true;
            }
            else
            {
                BOOST_TEST(config);
                BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
                BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
                BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
                BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
                BOOST_CHECK_EQUAL(config->hash().hex(), h256(5).hex());
            }
        });
}


BOOST_AUTO_TEST_CASE(getCode)
{
    auto scheduler =
        std::make_shared<SchedulerImpl>(executorManager, ledger, storage, executionMessageFactory,
            blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false, 0);
    auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
    scheduler->setBlockExecutiveFactory(blockExecutiveFactory);

    // Add executor
    auto executor = std::make_shared<MockDmcExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);
    SCHEDULER_LOG(DEBUG) << "----- add Executor ------";
    scheduler->getCode("hello world!", [](Error::Ptr error, bcos::bytes code) {
        BOOST_TEST(!error);
        BOOST_TEST(code.empty());
    });
}

BOOST_AUTO_TEST_CASE(call)
{
    // Add executor
    auto scheduler =
        std::make_shared<SchedulerImpl>(executorManager, ledger, storage, executionMessageFactory,
            blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false, 0);
    // auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
    // scheduler->setBlockExecutiveFactory(blockExecutiveFactory);
    auto executor = std::make_shared<MockParallelExecutorForCall>("executor1");
    executorManager->addExecutor("executor1", executor);

    std::string inputStr = "Hello world! request";
    bcos::crypto::KeyPairInterface::Ptr keyPair =
        blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
    auto tx = blockFactory->transactionFactory()->createTransaction(0, "address_to",
        bytes(inputStr.begin(), inputStr.end()), std::to_string(200), 300, "chain", "group", 500,
        keyPair);

    auto empty_to = blockFactory->transactionFactory()->createTransaction(0, "",
        bytes(inputStr.begin(), inputStr.end()), std::to_string(200), 300, "chain", "group", 500,
        keyPair);

    // call
    {
        bcos::protocol::TransactionReceipt::Ptr receipt;

        scheduler->call(tx,
            [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr receiptResponse) {
                auto blockNumber = receiptResponse->blockNumber();
                std::cout << "blockNumber" << blockNumber << std::endl;
                BOOST_TEST(!error);
                BOOST_TEST(receiptResponse);

                receipt = std::move(receiptResponse);
            });

        BOOST_CHECK_NE(receipt->blockNumber(), 0);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_GT(receipt->gasUsed(), 0);
        auto output = receipt->output();

        std::string outputStr((char*)output.data(), output.size());
        SCHEDULER_LOG(DEBUG) << LOG_KV("outputStr", outputStr);
        BOOST_CHECK_EQUAL(outputStr, "Hello world! response");
    }

    // call empty to
    {
        scheduler->call(empty_to,
            [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr receiptResponse) {
                BOOST_TEST(error);
                BOOST_TEST(error->errorMessage() == "Call address is empty");
                BOOST_TEST(receiptResponse == nullptr);
            });
    }
}


BOOST_AUTO_TEST_CASE(testDeploySysContract)
{
    auto scheduler =
        std::make_shared<SchedulerImpl>(executorManager, ledger, storage, executionMessageFactory,
            blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false, 0);
    // Add executor
    auto executor1 = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor1);

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(0);
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());

    auto tx = blockFactory->transactionFactory()->createTransaction(0,
        std::string(precompiled::AUTH_COMMITTEE_ADDRESS), {}, std::to_string(1), 500, "chainId",
        "groupId", utcTime());
    block->appendTransaction(std::move(tx));

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    scheduler->executeBlock(block, false,
        [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header, bool) {
            // callback(BCOS_ERROR_UNIQUE_PTR(-1, "deploy sys contract!"), nullptr);
            BOOST_TEST(error == nullptr);
            executedHeader.set_value(std::move(header));
        });
    auto header = executedHeader.get_future().get();


    BOOST_TEST(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());
}

BOOST_AUTO_TEST_CASE(testCallSysContract)
{
    auto scheduler =
        std::make_shared<SchedulerImpl>(executorManager, ledger, storage, executionMessageFactory,
            blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false, 0);
    // Add executor
    auto executor1 = std::make_shared<MockParallelExecutorForCall>("executor1");
    executorManager->addExecutor("executor1", executor1);

    auto tx = blockFactory->transactionFactory()->createTransaction(0,
        std::string(precompiled::AUTH_COMMITTEE_ADDRESS), {}, std::to_string(1), 500, "chainId",
        "groupId", utcTime());

    bcos::protocol::TransactionReceipt::Ptr receipt;

    scheduler->call(
        tx, [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr receiptResponse) {
            BOOST_TEST(!error);
            BOOST_TEST(receiptResponse);

            receipt = std::move(receiptResponse);
        });
    BOOST_CHECK_NE(receipt->blockNumber(), 0);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_GT(receipt->gasUsed(), 0);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test