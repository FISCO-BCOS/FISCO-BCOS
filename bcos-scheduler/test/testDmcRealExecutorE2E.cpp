/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file testDmcRealExecutorE2E.cpp
 * @brief BlockExecutive-level pin: a real TransactionExecutor executing a contract with
 *        two nested CREATEs must derive the addresses the scheduler recorded as
 *        f(currentSeq), including the CREATE that follows a resume.
 */

#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-executor/test/unittest/mock/MockLedger.h"
#include "bcos-executor/test/unittest/mock/MockTransactionalStorage.h"
#include "bcos-executor/test/unittest/mock/MockTxPool.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/executor/NativeExecutionMessage.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/src/DmcStepRecorder.h"
#include "bcos-scheduler/src/ExecutorManager.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-storage/RocksDBStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageFactory.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "executor/TransactionExecutorFactory.h"
#include "mock/MockLedger3.h"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::scheduler;
using namespace bcos::storage;
using namespace bcostars::protocol;

namespace bcos::test
{
struct DmcRealExecutorFixture
{
    DmcRealExecutorFixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        ioServicePool = std::make_shared<IOServicePool>(1, "dmc-e2e");
        hashImpl = std::make_shared<Keccak256>();
        auto signature = std::make_shared<Secp256k1Crypto>();
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signature, nullptr);

        executorLedger = std::make_shared<MockLedger>();
        ledger = std::make_shared<MockLedger3>();
        txPool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        auto lru = std::make_shared<LRUStateStorage>(backend, false);
        auto stateFactory = std::make_shared<StateStorageFactory>(0);
        auto messageFactory = std::make_shared<executor::NativeExecutionMessageFactory>();
        realExecutor = executor::TransactionExecutorFactory::build(
            executorLedger, txPool, lru, backend, messageFactory, stateFactory, hashImpl, false);

        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        auto secretKey =
            fromHex("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        memcpy(keyPair->secretKey()->mutableData(), secretKey.data(), 32);
        auto publicKey = fromHex(
            "ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
            "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae");
        memcpy(keyPair->publicKey()->mutableData(), publicKey.data(), 64);

        transactionFactory = std::make_shared<TransactionFactoryImpl>(cryptoSuite);
        receiptFactory = std::make_shared<TransactionReceiptFactoryImpl>(cryptoSuite);
        blockHeaderFactory = std::make_shared<BlockHeaderFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
        submitResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();

        executorManager = std::make_shared<ExecutorManager>(ioService);
        executorManager->addExecutor("executor1", realExecutor);
        rocksdb::DB* db = nullptr;
        rocksdb::Options options;
        options.create_if_missing = true;
        dbPath =
            std::filesystem::temp_directory_path() / ("dmc-e2e-" + std::to_string(bcos::utcTime()));
        auto status = rocksdb::DB::Open(options, dbPath.string(), &db);
        BOOST_REQUIRE(status.ok());
        storage = std::make_shared<RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);
        scheduler = std::make_shared<SchedulerImpl>(executorManager, ledger, storage,
            std::make_shared<executor::NativeExecutionMessageFactory>(), blockFactory, txPool,
            submitResultFactory, hashImpl, false, false, false, 0, ioServicePool);
    }

    ~DmcRealExecutorFixture()
    {
        scheduler = nullptr;
        ioServicePool = nullptr;
        std::error_code ignored;
        std::filesystem::remove_all(dbPath, ignored);
    }

    bcos::executor::TransactionExecutor::Ptr realExecutor;
    std::shared_ptr<MockTxPool> txPool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<MockLedger> executorLedger;
    std::shared_ptr<MockLedger3> ledger;
    SchedulerImpl::Ptr scheduler;
    ExecutorManager::Ptr executorManager;
    IOServicePool::Ptr ioServicePool;
    boost::asio::io_context ioService;
    Keccak256::Ptr hashImpl;
    CryptoSuite::Ptr cryptoSuite;
    KeyPairInterface::Ptr keyPair;
    BlockFactoryImpl::Ptr blockFactory;
    BlockHeaderFactoryImpl::Ptr blockHeaderFactory;
    TransactionFactoryImpl::Ptr transactionFactory;
    TransactionReceiptFactoryImpl::Ptr receiptFactory;
    TransactionSubmitResultFactory::Ptr submitResultFactory;
    storage::TransactionalStorageInterface::Ptr storage;
    std::filesystem::path dbPath;
    int64_t gas = 3000000;
};

BOOST_FIXTURE_TEST_SUITE(DmcRealExecutorSuite, DmcRealExecutorFixture)

BOOST_AUTO_TEST_CASE(nestedCreateAddressMatchesSchedulerCurrentSeq)
{
    std::string const aHex =
        "6022600c60003960226000f3"
        "600c6016600039"
        "600c60006000f0"
        "600c60006000f0"
        "00"
        "60006000526001601f6000f3";
    bytes initCode;
    boost::algorithm::unhex(aHex, std::back_inserter(initCode));

    auto header = std::make_shared<BlockHeaderImpl>();
    header->setNumber(1);
    header->setParentInfo(ParentInfo{.blockNumber = 0, .blockHash = h256(0)});
    header->calculateHash(*hashImpl);
    std::promise<void> nextPromise;
    realExecutor->nextBlockHeader(0, header, [&](Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    auto deployTx =
        fakeTransaction(cryptoSuite, keyPair, "", initCode, std::to_string(301), 100301, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(deployTx->sender()));
    auto deployHash = deployTx->hash();
    txPool->hash2Transaction.emplace(deployHash, deployTx);

    auto deploy = std::make_unique<executor::NativeExecutionMessage>();
    deploy->setContextID(300);
    deploy->setSeq(1300);
    deploy->setDepth(0);
    deploy->setOrigin(std::string(sender));
    deploy->setFrom(std::string(sender));
    deploy->setTo("cc2233445566778899001122334455667788990011");
    deploy->setGasAvailable(gas);
    deploy->setData(initCode);
    deploy->setType(executor::NativeExecutionMessage::TXHASH);
    deploy->setTransactionHash(deployHash);
    deploy->setCreate(true);
    std::promise<ExecutionMessage::UniquePtr> deployPromise;
    realExecutor->dmcExecuteTransaction(
        std::move(deploy), [&](Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            deployPromise.set_value(std::move(result));
        });
    auto deployResult = deployPromise.get_future().get();
    BOOST_REQUIRE(deployResult);
    auto const contractAddress = std::string(deployResult->newEVMContractAddress());
    BOOST_REQUIRE_GT(contractAddress.size(), 0);

    // The transaction under test calls the deployed contract.
    auto callTx = fakeTransaction(
        cryptoSuite, keyPair, contractAddress, bytes{}, std::to_string(302), 100302, "1", "1");
    auto callHash = callTx->hash();
    txPool->hash2Transaction.emplace(callHash, callTx);

    auto block = blockFactory->createBlock();
    auto blockHeader = block->blockHeader();
    blockHeader->setNumber(2);
    blockHeader->setParentInfo(ParentInfo{.blockNumber = 1, .blockHash = h256(1)});
    blockHeader->calculateHash(*hashImpl);
    block->appendTransactionMetaData(
        std::make_shared<TransactionMetaDataImpl>(callHash, contractAddress));

    BlockExecutive blockExecutive(
        block, scheduler.get(), 0, submitResultFactory, false, blockFactory, txPool);
    blockExecutive.dmcRecorder()->enableScheduleOutRecording();
    std::promise<Error::UniquePtr> resultPromise;
    blockExecutive.asyncExecute([&](Error::UniquePtr error, BlockHeader::Ptr executed, bool) {
        BOOST_CHECK(!error);
        BOOST_CHECK(executed);
        resultPromise.set_value(std::move(error));
    });
    auto error = resultPromise.get_future().get();
    BOOST_REQUIRE_MESSAGE(
        !error, "block execution failed: " << (error ? error->errorMessage() : ""));

    auto creates = blockExecutive.dmcRecorder()->scheduleOuts();
    std::vector<DmcStepRecorder::ScheduleOutRecord> createRecords;
    for (auto const& record : creates)
    {
        if (record.create && record.type == ExecutionMessage::MESSAGE)
            createRecords.push_back(record);
    }
    BOOST_REQUIRE_EQUAL(createRecords.size(), 2u);
    BOOST_CHECK_NE(createRecords[0].target, createRecords[1].target);
    for (auto const& record : createRecords)
    {
        BOOST_CHECK_EQUAL(
            record.target, bcos::newEVMAddress(*hashImpl, 2, record.contextID, record.currentSeq));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
