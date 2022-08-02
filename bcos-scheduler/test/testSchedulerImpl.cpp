#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-storage/src/RocksDBStorage.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "mock/MockBlockExecutive.h"
#include "mock/MockBlockExecutiveFactory.h"
#include "mock/MockDmcExecutor.h"
#include "mock/MockLedger3.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
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
    bcos::ledger::LedgerInterface::Ptr ledger;
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
    // executeBlock
    bcos::protocol::BlockHeader::Ptr blockHeader;

    SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHash", block)
                         << LOG_KV("BlockHeaderNumber", block->blockHeader()->number());

    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            if (error)
            {
                executeBlockError = true;
                BOOST_CHECK(error);
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_CHECK(!error);
                BOOST_CHECK(header);
                blockHeader = std::move(header);
            }
        });
    BOOST_CHECK(executeBlockError);
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
        // executeBlock
        bcos::protocol::BlockHeader::Ptr blockHeader;
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHash", block)
                             << LOG_KV("BlockHeaderNumber", block->blockHeader()->number());

        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
                if (error)
                {
                    BOOST_CHECK(error);
                    executeBlockError = true;
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_CHECK(!error);
                    BOOST_CHECK(header);
                    blockHeader = std::move(header);
                }
            });
        if (!executeBlockError)
        {
            BOOST_CHECK(blockHeader);
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
        bcos::protocol::BlockHeader::Ptr executeHeader1;
        // execute olderBlock whenQueueFront whenInQueue
        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                if (error)
                {
                    executeBlockError = true;
                    BOOST_CHECK(error);
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_CHECK(!error);
                    BOOST_CHECK(header);
                    executeHeader1 = std::move(header);
                }
            });
        if (!executeBlockError)
        {
            BOOST_CHECK(executeHeader1);
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
    bcos::protocol::BlockHeader::Ptr executeHeader11;
    // requestBlock = backNumber + 1
    scheduler->executeBlock(block11, false,
        [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
            if (error)
            {
                BOOST_CHECK(error);
                executeBlockError = true;
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_CHECK(!error);
                BOOST_CHECK(header);
                executeHeader11 = std::move(header);
            }
        });
    BOOST_CHECK(executeBlockError);
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
        // executeBlock
        bcos::protocol::BlockHeader::Ptr blockHeader;
        scheduler->executeBlock(block, false,
            [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                SCHEDULER_LOG(DEBUG) << LOG_KV("BlockHeader", header);
                if (error)
                {
                    executeBlockError = true;
                    BOOST_CHECK(error);
                    SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
                }
                else
                {
                    BOOST_CHECK(!error);
                    BOOST_CHECK(header);
                    blockHeader = std::move(header);
                }
            });
        if (!executeBlockError)
        {
            BOOST_CHECK(blockHeader);
        }
        executeBlockError = false;
        blockHeader = nullptr;
    }
    // commit
    bool commitBlockError = false;
    size_t errorNumber = 0;
    size_t queueFrontNumber = 0;
    for (size_t i = 5; i < 11; ++i)
    {
        auto blockHeader = blockHeaderFactory->createBlockHeader();
        blockHeader->setNumber(i);
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
                    BOOST_CHECK(config);
                    BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
                    BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
                    BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
                    BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
                    BOOST_CHECK_EQUAL(config->hash().hex(), h256(5).hex());
                    committedPromise.set_value(true);
                }
            });
        if (commitBlockError)
        {
            commitBlockError = false;
        }
    }
    BOOST_CHECK_EQUAL(errorNumber, 2);
    BOOST_CHECK_EQUAL(queueFrontNumber, 4);
    BOOST_CHECK(!commitBlockError);
    SCHEDULER_LOG(DEBUG) << LOG_KV("errorNumber", errorNumber)
                         << LOG_KV("queueFrontNumber", queueFrontNumber);

    // commit blockNumber <= 5
    auto blockHeader0 = blockHeaderFactory->createBlockHeader();
    blockHeader0->setNumber(0);
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
                BOOST_CHECK(config);
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
        block, false, [&](bcos::Error::Ptr&& error) { BOOST_CHECK(!error); });


    // executeBlock
    bool executeBlockError = false;
    bcos::protocol::BlockHeader::Ptr blockHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            if (error)
            {
                executeBlockError = true;
                BOOST_CHECK(error);
                SCHEDULER_LOG(ERROR) << "ExecuteBlock callback error";
            }
            else
            {
                BOOST_CHECK(!error);
                BOOST_CHECK(header);
                blockHeader = std::move(header);
            }
        });

    BOOST_CHECK(blockHeader);


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
                BOOST_CHECK(config);
                BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
                BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
                BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
                BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
                BOOST_CHECK_EQUAL(config->hash().hex(), h256(5).hex());
            }
        });

    BOOST_CHECK(!commitBlockError);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test