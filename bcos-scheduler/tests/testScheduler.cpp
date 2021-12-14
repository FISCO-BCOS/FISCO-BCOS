#include "ExecutorManager.h"
#include "SchedulerImpl.h"
#include "interfaces/crypto/CryptoSuite.h"
#include "interfaces/crypto/KeyPairInterface.h"
#include "interfaces/executor/ExecutionMessage.h"
#include "interfaces/ledger/LedgerInterface.h"
#include "interfaces/protocol/BlockHeaderFactory.h"
#include "interfaces/protocol/ProtocolTypeDef.h"
#include "interfaces/protocol/Transaction.h"
#include "interfaces/protocol/TransactionReceipt.h"
#include "interfaces/protocol/TransactionReceiptFactory.h"
#include "interfaces/protocol/TransactionSubmitResult.h"
#include "interfaces/storage/StorageInterface.h"
#include "libprotocol/TransactionSubmitResultFactoryImpl.h"
#include "mock/MockDeadLockExecutor.h"
#include "mock/MockExecutor.h"
#include "mock/MockExecutor3.h"
#include "mock/MockExecutorForCall.h"
#include "mock/MockExecutorForCreate.h"
#include "mock/MockExecutorForMessageDAG.h"
#include "mock/MockLedger.h"
#include "mock/MockMultiParallelExecutor.h"
#include "mock/MockRPC.h"
#include "mock/MockTransactionalStorage.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-framework/libexecutor/NativeExecutionMessage.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/latch.hpp>
#include <future>
#include <memory>

namespace bcos::test
{
struct SchedulerFixture
{
    SchedulerFixture()
    {
        hashImpl = std::make_shared<Keccak256Hash>();
        signature = std::make_shared<Secp256k1SignatureImpl>();
        suite = std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signature, nullptr);

        ledger = std::make_shared<MockLedger>();
        executorManager = std::make_shared<scheduler::ExecutorManager>();
        storage = std::make_shared<MockTransactionalStorage>();

        auto stateStorage = std::make_shared<storage::StateStorage>(nullptr);
        storage->m_storage = stateStorage;

        transactionFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite);
        transactionReceiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
        executionMessageFactory = std::make_shared<bcos::executor::NativeExecutionMessageFactory>();

        blockHeaderFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            suite, blockHeaderFactory, transactionFactory, transactionReceiptFactory);

        transactionSubmitResultFactory =
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>();
        auto notifier = [/*latch = &latch*/](bcos::protocol::BlockNumber,
                            bcos::protocol::TransactionSubmitResultsPtr _results,
                            std::function<void(Error::Ptr)> _callback) {
            SCHEDULER_LOG(TRACE) << "Submit callback execute, results size:" << _results->size();
            if (_callback)
            {
                _callback(nullptr);
            }
            // BOOST_CHECK(_results->size() == 8000);
            /*BOOST_CHECK_EQUAL(result->status(), 0);
            BOOST_CHECK_NE(result->blockHash(), h256(0));
            BOOST_CHECK(result->transactionReceipt());
            BOOST_CHECK_LT(result->transactionIndex(), 1000 * 8);*/

            // auto receipt = result->transactionReceipt();
            // auto output = receipt->output();
            // std::string_view outputStr((char*)output.data(), output.size());
            // BOOST_CHECK_EQUAL(outputStr, "Hello world!");
            /*if (latch)
            {
                latch->get()->count_down();
            }*/
        };

        scheduler = std::make_shared<scheduler::SchedulerImpl>(executorManager, ledger, storage,
            executionMessageFactory, blockFactory, transactionSubmitResultFactory, hashImpl, true);

        std::dynamic_pointer_cast<scheduler::SchedulerImpl>(scheduler)->registerTransactionNotifier(
            std::move(notifier));

        keyPair = suite->signatureImpl()->generateKeyPair();
    }

    ledger::LedgerInterface::Ptr ledger;
    scheduler::ExecutorManager::Ptr executorManager;
    std::shared_ptr<MockTransactionalStorage> storage;
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory;
    protocol::TransactionReceiptFactory::Ptr transactionReceiptFactory;
    protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
    bcos::crypto::Hash::Ptr hashImpl;
    scheduler::SchedulerImpl::Ptr scheduler;
    bcos::crypto::KeyPairInterface::Ptr keyPair;

    bcostars::protocol::TransactionFactoryImpl::Ptr transactionFactory;
    bcos::crypto::SignatureCrypto::Ptr signature;
    bcos::crypto::CryptoSuite::Ptr suite;
    bcostars::protocol::BlockFactoryImpl::Ptr blockFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory;

    std::unique_ptr<boost::latch> latch;
};

BOOST_FIXTURE_TEST_SUITE(Scheduler, SchedulerFixture)

BOOST_AUTO_TEST_CASE(executeBlock)
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

    BOOST_CHECK(executedHeader);
    BOOST_CHECK_NE(executedHeader->stateRoot(), h256());

    bcos::protocol::BlockNumber notifyBlockNumber = 0;
    scheduler->registerBlockNumberReceiver(
        [&](protocol::BlockNumber number) { notifyBlockNumber = number; });

    bool committed = false;
    scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
            committed = true;

            BOOST_CHECK(!error);
            BOOST_CHECK(config);
            BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
            BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
            BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
            BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
            BOOST_CHECK_EQUAL(config->hash().hex(), h256(110).hex());
        });

    BOOST_CHECK(committed);
    BOOST_CHECK_EQUAL(notifyBlockNumber, 100);
}

BOOST_AUTO_TEST_CASE(parallelExecuteBlock)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockMultiParallelExecutor>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t i = 0; i < 100; ++i)
    {
        for (size_t j = 0; j < 8; ++j)
        {
            auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
                h256((i + 1) * (j + 1)), "contract" + boost::lexical_cast<std::string>(j));
            // metaTx->setSource("i am a source!");
            block->appendTransactionMetaData(std::move(metaTx));
        }
    }

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;

    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);

            executedHeader.set_value(std::move(header));
        });

    auto header = executedHeader.get_future().get();

    BOOST_CHECK(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());

    bcos::protocol::BlockNumber notifyBlockNumber = 0;
    scheduler->registerBlockNumberReceiver(
        [&](protocol::BlockNumber number) { notifyBlockNumber = number; });
    std::promise<void> commitPromise;
    scheduler->commitBlock(
        header, [&](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&& config) {
            BOOST_CHECK(!error);
            BOOST_CHECK(config);
            BOOST_CHECK_EQUAL(config->blockTxCountLimit(), 100);
            BOOST_CHECK_EQUAL(config->leaderSwitchPeriod(), 300);
            BOOST_CHECK_EQUAL(config->consensusNodeList().size(), 1);
            BOOST_CHECK_EQUAL(config->observerNodeList().size(), 2);
            BOOST_CHECK_EQUAL(config->hash().hex(), h256(110).hex());

            commitPromise.set_value();
        });

    commitPromise.get_future().get();

    BOOST_CHECK_EQUAL(notifyBlockNumber, 100);
}

BOOST_AUTO_TEST_CASE(keyLocks)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockMultiParallelExecutor>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t i = 0; i < 1000; ++i)
    {
        for (size_t j = 0; j < 8; ++j)
        {
            auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
                h256(i * j), "contract" + boost::lexical_cast<std::string>(j));
            block->appendTransactionMetaData(std::move(metaTx));
        }
    }
}

BOOST_AUTO_TEST_CASE(call)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockParallelExecutorForCall>("executor1"));

    std::string inputStr = "Hello world! request";
    auto tx = blockFactory->transactionFactory()->createTransaction(0, "address_to",
        bytes(inputStr.begin(), inputStr.end()), 200, 300, "chain", "group", 500, keyPair);

    bcos::protocol::TransactionReceipt::Ptr receipt;

    scheduler->call(
        tx, [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr receiptResponse) {
            BOOST_CHECK(!error);
            BOOST_CHECK(receiptResponse);

            receipt = std::move(receiptResponse);
        });

    BOOST_CHECK_EQUAL(receipt->blockNumber(), 0);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_GT(receipt->gasUsed(), 0);
    auto output = receipt->output();

    std::string outputStr((char*)output.data(), output.size());
    BOOST_CHECK_EQUAL(outputStr, "Hello world! response");
}

BOOST_AUTO_TEST_CASE(registerExecutor)
{
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    auto executor2 = std::make_shared<MockParallelExecutor>("executor2");

    scheduler->registerExecutor(
        "executor1", executor, [&](Error::Ptr&& error) { BOOST_CHECK(!error); });
    scheduler->registerExecutor(
        "executor2", executor2, [&](Error::Ptr&& error) { BOOST_CHECK(!error); });
}

BOOST_AUTO_TEST_CASE(createContract)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockParallelExecutorForCreate>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
    metaTx->setHash(h256(1));
    metaTx->setTo("");
    block->appendTransactionMetaData(std::move(metaTx));

    bcos::protocol::BlockHeader::Ptr executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);

            executedHeader = std::move(header);
        });

    BOOST_CHECK(executedHeader);
    BOOST_CHECK_NE(executedHeader->stateRoot(), h256());
}

BOOST_AUTO_TEST_CASE(dag)
{
    // Add executor
    executorManager->addExecutor("executor1", std::make_shared<MockParallelExecutor>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    for (size_t i = 0; i < 1000; ++i)
    {
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            h256(i + 1), "contract" + boost::lexical_cast<std::string>((i + 1) % 10));
        metaTx->setAttribute(metaTx->attribute() | bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransactionMetaData(std::move(metaTx));
    }

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);

            executedHeader.set_value(std::move(header));
        });

    auto header = executedHeader.get_future().get();

    BOOST_CHECK(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());
}

BOOST_AUTO_TEST_CASE(dagByMessage)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockParallelExecutorByMessage>("executor1"));

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(100);

    auto keyPair = blockFactory->cryptoSuite()->signatureImpl()->generateKeyPair();
    for (size_t i = 0; i < 1000; ++i)
    {
        bytes input;
        auto tx = transactionFactory->createTransaction(20,
            "contract" + boost::lexical_cast<std::string>((i + 1) % 10), input, 100, 200, "chainID",
            "groupID", 400, keyPair);
        tx->setAttribute(bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransaction(tx);
    }

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);

            executedHeader.set_value(std::move(header));
        });

    auto header = executedHeader.get_future().get();

    BOOST_CHECK(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());
}

BOOST_AUTO_TEST_CASE(executedBlock)
{
    // Add executor
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);

    size_t count = 10;
    std::vector<bcos::crypto::HashType> hashes;
    for (size_t blockNumber = 0; blockNumber < count; ++blockNumber)
    {
        // Generate a test block
        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(blockNumber);

        for (size_t i = 0; i < 1000; ++i)
        {
            auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
                h256(i + 1), "contract" + boost::lexical_cast<std::string>((i + 1) % 10));
            metaTx->setAttribute(metaTx->attribute() | bcos::protocol::Transaction::Attribute::DAG);
            block->appendTransactionMetaData(std::move(metaTx));
        }

        std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
        scheduler->executeBlock(
            block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
                BOOST_CHECK(!error);
                BOOST_CHECK(header);

                executedHeader.set_value(std::move(header));
            });

        auto header = executedHeader.get_future().get();

        BOOST_CHECK(header);
        BOOST_CHECK_NE(header->stateRoot(), h256());

        SCHEDULER_LOG(TRACE) << "StateRoot: " << header->stateRoot();

        hashes.emplace_back(header->stateRoot());

        executor->clear();
    }

    for (size_t blockNumber = 0; blockNumber < count; ++blockNumber)
    {
        // Get the executed block
        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(blockNumber);

        scheduler->executeBlock(
            block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
                BOOST_CHECK(!error);
                BOOST_CHECK_EQUAL(header->stateRoot().hex(), hashes[blockNumber].hex());
            });
    }
}

BOOST_AUTO_TEST_CASE(testDeploySysContract)
{
    // Add executor
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);

    protocol::BlockNumber blockNumber = 0;

    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(blockNumber);

    auto tx = blockFactory->transactionFactory()->createTransaction(
        3, precompiled::AUTH_COMMITTEE_ADDRESS, {}, u256(1), 500, "chainId", "groupId", utcTime());
    block->appendTransaction(std::move(tx));

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            // callback(BCOS_ERROR_UNIQUE_PTR(-1, "deploy sys contract!"), nullptr);
            BOOST_CHECK(error == nullptr);
            executedHeader.set_value(std::move(header));
        });

    auto header = executedHeader.get_future().get();

    BOOST_CHECK(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());
}

BOOST_AUTO_TEST_CASE(testCallSysContract)
{
    // Add executor
    executorManager->addExecutor(
        "executor1", std::make_shared<MockParallelExecutorForCall>("executor1"));

    auto tx = blockFactory->transactionFactory()->createTransaction(
        3, precompiled::AUTH_COMMITTEE_ADDRESS, {}, u256(1), 500, "chainId", "groupId", utcTime());

    bcos::protocol::TransactionReceipt::Ptr receipt;

    scheduler->call(
        tx, [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr receiptResponse) {
            BOOST_CHECK(!error);
            BOOST_CHECK(receiptResponse);

            receipt = std::move(receiptResponse);
        });
    BOOST_CHECK_EQUAL(receipt->blockNumber(), 0);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_GT(receipt->gasUsed(), 0);
}

BOOST_AUTO_TEST_CASE(checkCommittedBlock)
{
    // Add executor
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);

    auto blockNumber = 100lu;
    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(blockNumber);

    for (size_t i = 0; i < 1000; ++i)
    {
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            h256(i + 1), "contract" + boost::lexical_cast<std::string>((i + 1) % 10));
        metaTx->setAttribute(metaTx->attribute() | bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransactionMetaData(std::move(metaTx));
    }

    std::promise<bcos::protocol::BlockHeader::Ptr> executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            BOOST_CHECK(!error);
            BOOST_CHECK(header);

            executedHeader.set_value(std::move(header));
        });

    auto header = executedHeader.get_future().get();

    BOOST_CHECK(header);
    BOOST_CHECK_NE(header->stateRoot(), h256());

    SCHEDULER_LOG(TRACE) << "StateRoot: " << header->stateRoot();
    auto commitHeader = blockHeaderFactory->createBlockHeader();
    commitHeader->setNumber(blockNumber);

    scheduler->commitBlock(commitHeader,
        [](bcos::Error::Ptr&& error, bcos::ledger::LedgerConfig::Ptr&&) { BOOST_CHECK(!error); });

    // Try execute a same block
    auto newHeader = blockHeaderFactory->createBlockHeader();
    newHeader->setNumber(blockNumber);
    block->setBlockHeader(newHeader);

    scheduler->executeBlock(
        block, false, [](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&&) {
            BOOST_CHECK(error);
            BOOST_CHECK_EQUAL(
                error->errorCode(), bcos::scheduler::SchedulerError::InvalidBlockNumber);
            BOOST_CHECK_GT(error->errorMessage().size(), 0);
        });
}

BOOST_AUTO_TEST_CASE(executeWithSystemError)
{
    // Add executor
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);

    auto blockNumber = 100lu;
    // Generate a test block
    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(blockNumber);

    for (size_t i = 0; i < 10; ++i)
    {
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            h256(i + 1), "contract" + boost::lexical_cast<std::string>((i + 1) % 10));
        // metaTx->setAttribute(metaTx->attribute() | bcos::protocol::Transaction::Attribute::DAG);
        block->appendTransactionMetaData(std::move(metaTx));
    }

    // Add an error transaction
    auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        h256(10086), "contract" + boost::lexical_cast<std::string>(10086));
    block->appendTransactionMetaData(std::move(metaTx));

    std::promise<void> executedHeader;
    scheduler->executeBlock(
        block, false, [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& header) {
            BOOST_CHECK(error);
            BOOST_CHECK_EQUAL(error->errorCode(), bcos::scheduler::SchedulerError::UnknownError);
            BOOST_CHECK_GT(error->errorMessage().size(), 0);
            BOOST_CHECK(!header);

            executedHeader.set_value();
        });

    executedHeader.get_future().get();
}

BOOST_AUTO_TEST_CASE(getCode)
{
    // Add executor
    auto executor = std::make_shared<MockParallelExecutor>("executor1");
    executorManager->addExecutor("executor1", executor);

    scheduler->getCode("hello world!", [](Error::Ptr error, bcos::bytes code) {
        BOOST_CHECK(!error);
        BOOST_CHECK(code.empty());
    });
}

BOOST_AUTO_TEST_CASE(executeWithDeadLock)
{
    auto executor = std::make_shared<MockDeadLockParallelExecutor>("executor10");
    executorManager->addExecutor("executor10", executor);

    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(900);
    for (size_t i = 0; i < 2; ++i)
    {
        auto metaTx = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            h256(i + 1), "contract" + boost::lexical_cast<std::string>(i + 1));
        block->appendTransactionMetaData(std::move(metaTx));
    }

    scheduler->executeBlock(
        block, false, [](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& blockHeader) {
            BOOST_CHECK(!error);
            BOOST_CHECK(blockHeader);
        });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test