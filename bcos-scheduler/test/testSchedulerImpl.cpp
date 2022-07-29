#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-storage/src/RocksDBStorage.h"
#include "mock/MockBlockExecutive.h"
#include "mock/MockBlockExecutiveFactory.h"
#include "mock/MockDmcExecutor.h"
#include "mock/MockLedger.h"
#include "mock/MockLedger2.h"
#include "mock/MockLedger3.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>
#include <optional>


using namespace std;
using namespace bcos;
using namespace bcos::storage;
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
        ledger = std::make_shared<MockLedger>();
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

        auto scheduler = std::make_shared<bcos::scheduler::SchedulerImpl>(executorManager, ledger,
            storage, executionMessageFactory, blockFactory, txPool, transactionSubmitResultFactory,
            hashImpl, false, false, false, 0);
        auto blockExecutiveFactory = std::make_shared<bcos::test::MockBlockExecutiveFactory>(false);
        scheduler->setBlockExecutiveFactory(blockExecutiveFactory);
    };
    ~schedulerImplFixture() {}
    bcos::ledger::LedgerInterface::Ptr ledger;
    bcos::scheduler::ExecutorManager::Ptr executorManager;
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory;
    protocol::TransactionReceiptFactory::Ptr transactionReceiptFactory;
    protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
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
    RocksDBStorage::Ptr storage = nullptr;
};

BOOST_FIXTURE_TEST_SUIT(testSchedulerImpl, schedulerImplFixture)

BOOST_AUTO_TEST_CASE(executeBlockTest)
{
    for (size_t i = 5; i < 10; ++i)
    {
        auto block = blockFactory->createBlock();
        block->blockHeader->setNumber(i);
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i);
        for (size_t j = 0; j < 10; ++j)
        {
            auto metaTx =
                std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract1");
            block->appendTransactionMetaData(std::move(metaTx));
        }
        // executeBlock
        bcos::protocol::BlockHeader::Ptr blockHeader;

        scheduler->executeBlock(block, false,
            [&](bcos::ERROR::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                BOOST_CHECK(header);
                blockHeader = std::move(header);
            });

        BOOST_CHECK(blockHeader);
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i)
                             << LOG_KV("executeBlock second, blockHeader", blockHeader->number());
        blockHeader = nullptr;
    }

    for (size_t i = 4; i < 7; i++)
    {
        auto block = blockFactory->createBlock();
        block->blockHeader->setNumber(i);
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
            [&](bcos::ERROR::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
                BOOST_CHECK(!error);
                BOOST_CHECK(header);
                executeHeader1 = std::move(header);
            });
        BOOST(executeHeader1);
        SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i)
                             << LOG_KV("executeBlock second, blockHeader", executeHeader1);
        executeHeader1 = nullptr;
    }

    auto block = blockFactory->createBlock();
    block->blockHeader->setNumber(10);
    for (size_t j = 0; j < 10; ++j)
    {
        auto metaTx =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(j), "contract2");
        block->appendTransactionMetaData(std::move(metaTx));
    }
    bcos::protocol::BlockHeader::Ptr executeHeader10;
    // requestBlock = backNumber + 1
    scheduler->executeBlock(
        block, false, [&](bcos::ERROR::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);
            executeHeader10 = std::move(header);
        });
    SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", i)
                         << LOG_KV("executeBlock second, blockHeader", executeHeader10);
    BOOST(executeHeader10);
}
BOOST_AUTO_TEST_CASE_END()
}  // namespace bcos::test
   // BOOST_AUTO_TEST_CASE(commitBlock)
   // {
   //     auto scheduler =
   //         std::make_shared<schedulerImpl>(executorManager, ledger, storage,
   //         executionMessageFactory,
//             blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false,
//             0);
//     auto blockExecutiveFactory =
//         std::make_shared<bcos::scheduler::MockBlockExecutiveFactory>(false);
//     // preExecuteBlock

//     // Fill m_block
//     for (size_t i = 100; i < 10; ++i)
//     {
//         auto block = blockFactory->createBlock();
//         block->blockHeader->setNumber(i);
//         for (size_t i = 0; i < 10; ++i)
//         {
//             auto metaTx =
//                 std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i),
//                 "contract1");
//             block->appendTransactionMetaData(std::move(metaTx));
//         }
//         bcos::protocol::BlockHeader::Ptr executeHeader1;
//         // execute olderBlock whenQueueFront whenInQueue
//         scheduler->executeBlock(block, false,
//             [&](bcos::ERROR::Ptr&& error, bcos::protocol::BlockHeader::Ptr header, bool) {
//                 BOOST_CHECK(!error);
//                 BOOST_CHECK(header);
//                 executeHeader1 = std::move(header);
//             });
//         BOOST(executeHeader1);
//         executeHeader1 = nullptr;
//     }
//     // commit
//     for (size_t i = 100; i < 10; ++i)
//     {
//         auto blockHeader = blockHeaderFactory->createHeader();
//         blockHeader->setNumber(i);
//         scheduler->commitBlock(
//             blockHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config)
//             {
//                 BOOST_CHECK(!error);
//                 BOOST_CHECK(config);
//                 BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
//                 BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
//                 BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
//                 BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
//                 BOOST_CHECK_EQUAL(config->hash().hex(), h256(110).hex());
//                 committedPromise.set_value(true);
//             });
//     }
// }
// BOOST_AUTO_TEST_CASE(handlerBlockTest)
// {
//     auto scheduler =
//         std::make_shared<SchedulerImpl>(executorManager, ledger, storage,
//         executionMessageFactory,
//             blockFactory, txPool, transactionSubmitResultFactory, hashImpl, false, false, false,
//             0);
//     auto blockExecutiveFactory =
//         std::make_shared<bcos::scheduler::MockBlockExecutiveFactory>(false);
//     scheduler->setBlockExecutiveFactory(blockExecutiveFactory);

//     // create Block
//     auto block = blockFactory->createBlock();
//     block->blockHeader->setNumber(100);

//     for (size_t i = 0; i < 10; ++i)
//     {
//         auto metaTx =
//             std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract1");
//         block->appendTransactionMetaData(std::move(metaTx));
//     }
//     for (size_t i = 10; i < 20; ++i)
//     {
//         auto metaTx =
//             std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract2");
//         block->appendTransactionMetaData(std::move(metaTx));
//     }
//     for (size_t i = 20; i < 30; ++i)
//     {
//         auto metaTx =
//             std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h256(i), "contract3");
//         block->appendTransactionMetaData(std::move(metaTx));
//     }
//     // preExecuteBlock
//     std::promise<bcos::protocol::BlockHeader::Ptr> executedHeaderPromise;
//     scheduler->preExecuteBlock(
//         block, false, [&](bcos::ERROR::Ptr&& error) { BOOST_CHECK(!error); });

//     auto blockNumber = block->blockHeaderConst()->number();
//     int64_t timestamp = block->blockHeaderConst()->timestamp();
//     BOOST_CHECK_EQUAL(scheduler->getPreparedBlock(blockNumber, timestamp));
//     BOOST_CHECK_EQUAL(blockNumber, 100);

//     // executeBlock
//     scheduler->executeBlock(
//         block, false, [&](bcos::ERROR::Ptr&& error, bcos::protocol::BlockHeader::Ptr header,
//         bool) {
//             BOOST_CHECK(!error);
//             BOOST_CHECK(header);
//             executeHeaderPromise.set_value(std::move(header));
//         });

//     bcos::protocol::BlockHeader::Ptr executedHeader = executeHeaderPromise.get_future().get();
//     BOOST_CHECK(executedHeader);
//     BOOST_CHECK_EQUAL(executedHeader->stateRoot(), h256());

//     // registerBlockNumberReceiver
//     bcos::protocol::BlockNumber notifyBlockNumber = 0;
//     scheduler->registerBlockNumberReceiver(
//         [&](protocol::BlockNumber number) { notifyBlockNumber = number; });

//     // commitBlock
//     std::promise<bool> committedPromise;
//     scheduler->commitBlock(
//         executedHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
//             BOOST_CHECK(!error);
//             BOOST_CHECK(config);
//             BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
//             BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
//             BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
//             BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
//             BOOST_CHECK_EQUAL(config->hash().hex(), h256(110).hex());
//             committedPromise.set_value(true);
//         });

//     bool committed = committedPromise.get_future().get();
//     BOOST_CHECK(committed);
//     BOOST_CHECK_EQUAL(notifyBlockNumber, 100);
// }