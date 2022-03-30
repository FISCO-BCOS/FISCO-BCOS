/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief unit test for the txpool
 * @file TxPoolTest.cpp
 * @author: yujiechen
 * @date 2021-05-26
 */
#include "interfaces/crypto/KeyPairInterface.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-protocol/testutils/protocol/FakeTransaction.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TestPromptFixture)
void testAsyncFillBlock(TxPoolFixture::Ptr _faker, TxPoolInterface::Ptr _txpool,
    TxPoolStorageInterface::Ptr _txpoolStorage, CryptoSuite::Ptr _cryptoSuite)
{
    // case1: miss all transactions and verify failed
    auto block = _faker->txpool()->txpoolConfig()->blockFactory()->createBlock();
    auto blockHeader =
        _faker->txpool()->txpoolConfig()->blockFactory()->blockHeaderFactory()->createBlockHeader();
    block->setBlockHeader(blockHeader);
    auto blockFactory = _faker->txpool()->txpoolConfig()->blockFactory();
    HashListPtr txsHash = std::make_shared<HashList>();
    for (size_t i = 0; i < 10; i++)
    {
        auto txHash = _cryptoSuite->hashImpl()->hash(std::to_string(i));
        // auto txMetaData = transactionMetaDataFactory.createTransactionMetaData(txHash,
        // txHash.abridged());
        auto txMetaData = _faker->blockFactory()->createTransactionMetaData();
        txMetaData->setHash(txHash);
        txMetaData->setTo(txHash.abridged());

        txsHash->emplace_back(txHash);
        block->appendTransactionMetaData(txMetaData);
    }
    bool finish = false;
    _txpool->asyncFillBlock(txsHash, [&](Error::Ptr _error, TransactionsPtr) {
        BOOST_CHECK(_error->errorCode() == CommonError::TransactionsMissing);
        finish = true;
    });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // verify block with invalid txsHash
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    finish = false;
    _txpool->asyncVerifyBlock(
        _faker->nodeID(), ref(*blockData), [&](Error::Ptr _error, bool _result) {
            BOOST_CHECK(_error->errorCode() == CommonError::TransactionsMissing);
            BOOST_CHECK(_result == false);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // case2: hit all the transactions and verify success
    auto txs = _txpoolStorage->fetchNewTxs(10000);
    block = _faker->txpool()->txpoolConfig()->blockFactory()->createBlock();
    blockHeader =
        _faker->txpool()->txpoolConfig()->blockFactory()->blockHeaderFactory()->createBlockHeader();
    block->setBlockHeader(blockHeader);
    BOOST_CHECK(txs->size() > 0);
    txsHash->clear();
    for (auto tx : *txs)
    {
        txsHash->emplace_back(tx->hash());
        // auto txMetaData =
        //     blockFactory->createTransactionMetaData(tx->hash(), tx->hash().abridged());
        auto txMetaData = _faker->blockFactory()->createTransactionMetaData();
        txMetaData->setHash(tx->hash());
        txMetaData->setTo(tx->hash().abridged());
        block->appendTransactionMetaData(txMetaData);
    }
    finish = false;
    _txpool->asyncFillBlock(txsHash, [&](Error::Ptr _error, TransactionsPtr _fetchedTxs) {
        BOOST_CHECK(_error == nullptr);
        BOOST_CHECK(txsHash->size() == _fetchedTxs->size());
        size_t i = 0;
        for (auto tx : *_fetchedTxs)
        {
            BOOST_CHECK((*txsHash)[i++] == tx->hash());
        }
        finish = true;
    });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // verify the blocks
    blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    finish = false;
    _txpool->asyncVerifyBlock(
        _faker->nodeID(), ref(*blockData), [&](Error::Ptr _error, bool _result) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_result == true);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // case3: with some txs hitted
    auto txHash = _cryptoSuite->hashImpl()->hash("test");
    txsHash->emplace_back(txHash);
    // auto txMetaData = blockFactory->createTransactionMetaData(txHash, txHash.abridged());
    auto txMetaData = _faker->blockFactory()->createTransactionMetaData();
    txMetaData->setHash(txHash);
    txMetaData->setTo(txHash.abridged());
    block->appendTransactionMetaData(txMetaData);

    finish = false;
    _txpool->asyncFillBlock(txsHash, [&](Error::Ptr _error, TransactionsPtr) {
        BOOST_CHECK(_error->errorCode() == CommonError::TransactionsMissing);
        finish = true;
    });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    finish = false;
    blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    std::cout << "#### test case3" << std::endl;
    _txpool->asyncVerifyBlock(
        _faker->nodeID(), ref(*blockData), [&](Error::Ptr _error, bool _result) {
            BOOST_CHECK(_error->errorCode() == CommonError::TransactionsMissing);
            BOOST_CHECK(_result == false);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void testAsyncSealTxs(TxPoolFixture::Ptr _faker, TxPoolInterface::Ptr _txpool,
    TxPoolStorageInterface::Ptr _txpoolStorage, int64_t _blockLimit)
{
    // asyncSealTxs
    auto originTxsSize = _txpoolStorage->size();
    size_t txsLimit = 10;
    HashListPtr sealedTxs = std::make_shared<HashList>();
    bool finish = false;
    _txpool->asyncSealTxs(
        txsLimit, nullptr, [&](Error::Ptr _error, Block::Ptr _txsMetaDataList, Block::Ptr) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == txsLimit);
            for (size_t i = 0; i < _txsMetaDataList->transactionsMetaDataSize(); i++)
            {
                sealedTxs->emplace_back(_txsMetaDataList->transactionHash(i));
            }
            BOOST_CHECK(_txpoolStorage->size() == originTxsSize);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // seal again to fetch all unsealed txs
    finish = false;
    _txpool->asyncSealTxs(
        100000, nullptr, [&](Error::Ptr _error, Block::Ptr _txsMetaDataList, Block::Ptr) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == (originTxsSize - txsLimit));
            BOOST_CHECK(_txpoolStorage->size() == originTxsSize);
            std::set<HashType> txsSet(sealedTxs->begin(), sealedTxs->end());
            for (size_t i = 0; i < _txsMetaDataList->transactionsMetaDataSize(); i++)
            {
                auto const& hash = _txsMetaDataList->transactionHash(i);
                BOOST_CHECK(!txsSet.count(hash));
            }
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    finish = false;
    _txpool->asyncMarkTxs(sealedTxs, false, 0, HashType(), [&](Error::Ptr _error) {
        BOOST_CHECK(_error == nullptr);
        finish = true;
    });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // seal again
    finish = false;
    _txpool->asyncSealTxs(
        100000, nullptr, [&](Error::Ptr _error, Block::Ptr _txsMetaDataList, Block::Ptr) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == sealedTxs->size());
            BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == sealedTxs->size());
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // test asyncNotifyBlockResult
    auto blockNumber = _faker->ledger()->blockNumber() + _blockLimit;
    auto txsResult = std::make_shared<TransactionSubmitResults>();
    for (auto txHash : *sealedTxs)
    {
        auto txResult = std::make_shared<TransactionSubmitResultImpl>();
        txResult->setTxHash(txHash);
        txResult->setStatus((uint32_t)TransactionStatus::None);
        txsResult->emplace_back(txResult);
    }
    auto missedTxs = std::make_shared<HashList>();
    auto notifiedTxs = _txpoolStorage->fetchTxs(*missedTxs, *sealedTxs);
    BOOST_CHECK(missedTxs->size() == 0);
    BOOST_CHECK(notifiedTxs->size() == sealedTxs->size());

    finish = false;
    _txpool->asyncNotifyBlockResult(blockNumber, txsResult, [&](Error::Ptr _error) {
        BOOST_CHECK(_error == nullptr);
        finish = true;
    });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // check the txpool size
    BOOST_CHECK(_txpoolStorage->size() == originTxsSize - sealedTxs->size());
    // check the txpoolNonce
    auto txPoolNonceChecker = _faker->txpool()->txpoolConfig()->txPoolNonceChecker();
    auto validator =
        std::dynamic_pointer_cast<TxValidator>(_faker->txpool()->txpoolConfig()->txValidator());
    auto ledgerNonceChecker = validator->ledgerNonceChecker();
    for (auto tx : *notifiedTxs)
    {
        BOOST_CHECK(txPoolNonceChecker->checkNonce(tx) == TransactionStatus::None);
        BOOST_CHECK(ledgerNonceChecker->checkNonce(tx) == TransactionStatus::NonceCheckFail);
    }
    // check the nonce of ledger->blockNumber() hash been removed from ledgerNonceChecker
    auto const& blockData = _faker->ledger()->ledgerData();
    auto const& nonceList = blockData[_faker->ledger()->blockNumber()]->nonces();
    for (auto const& nonce : *nonceList)
    {
        BOOST_CHECK(ledgerNonceChecker->exists(nonce) == false);
    }

    // case: the other left txs expired for invalid blockLimit
    finish = false;
    std::cout << "######### ayncSeal with invalid blocklimit" << std::endl;
    std::cout << "##### origin txsSize:" << _txpoolStorage->size() << std::endl;

    _txpool->asyncResetTxPool(nullptr);
    _txpool->asyncSealTxs(
        100000, nullptr, [&](Error::Ptr _error, Block::Ptr _txsMetaDataList, Block::Ptr) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == 0);
            BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == 0);
            finish = true;
        });
    while (!finish || (_txpoolStorage->size() > 0))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    BOOST_CHECK(_txpoolStorage->size() == 0);
}

void txPoolInitAndSubmitTransactionTest(bool _sm, CryptoSuite::Ptr _cryptoSuite)
{
    auto signatureImpl = _cryptoSuite->signatureImpl();
    auto hashImpl = _cryptoSuite->hashImpl();
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), _cryptoSuite, groupId, chainId, blockLimit, fakeGateWay);
    faker->init();

    // check the txpool config
    auto txpoolConfig = faker->txpool()->txpoolConfig();
    BOOST_CHECK(txpoolConfig->txPoolNonceChecker());
    BOOST_CHECK(txpoolConfig->txValidator());
    BOOST_CHECK(txpoolConfig->blockFactory());
    BOOST_CHECK(txpoolConfig->txFactory());
    BOOST_CHECK(txpoolConfig->ledger());

    auto txpool = faker->txpool();
    auto txpoolStorage = txpool->txpoolStorage();
    // case1: the node is not in the consensus/observerList
    auto tx = fakeTransaction(_cryptoSuite, utcTime());
    checkTxSubmit(txpool, txpoolStorage, tx, HashType(),
        (uint32_t)TransactionStatus::RequestNotBelongToTheGroup, 0);

    // case2: transaction with invalid blockLimit
    faker->appendSealer(faker->nodeID());
    auto ledger = faker->ledger();
    tx = fakeTransaction(_cryptoSuite, utcTime() + 11000, ledger->blockNumber() + blockLimit + 1,
        faker->chainId(), faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::BlockLimitCheckFail, 0);

    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto const& blockData = ledger->ledgerData();
    auto duplicatedNonce =
        blockData[ledger->blockNumber() - blockLimit + 1]->transaction(0)->nonce();
    tx = fakeTransaction(_cryptoSuite, duplicatedNonce, ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::NonceCheckFail, 0);

    // case4: invalid groupId
    tx = fakeTransaction(_cryptoSuite, utcTime(), ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), "invalidGroup");
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidGroupId, 0);

    // case5: invalid chainId
    tx = fakeTransaction(_cryptoSuite, utcTime(), ledger->blockNumber() + blockLimit - 4,
        "invalidChainId", faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidChainId, 0);

    // case6: invalid signature
    tx = fakeTransaction(_cryptoSuite, utcTime() + 100000, ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), faker->groupId());

    auto pbTx = std::dynamic_pointer_cast<PBTransaction>(tx);
    bcos::crypto::KeyPairInterface::Ptr invalidKeyPair = signatureImpl->generateKeyPair();
    auto invalidHash = hashImpl->hash(std::string("test"));
    auto signatureData = signatureImpl->sign(*invalidKeyPair, invalidHash, true);
    pbTx->updateSignature(ref(*signatureData), bytes());
    size_t importedTxNum = 0;
    if (!_sm)
    {
        importedTxNum++;
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(), (uint32_t)TransactionStatus::None,
            importedTxNum, false, false, true);
    }
    else
    {
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(),
            (uint32_t)TransactionStatus::InvalidSignature, importedTxNum);
    }

    // case7: submit success
    importedTxNum++;
    tx = fakeTransaction(_cryptoSuite, utcTime() + 2000000, ledger->blockNumber() + blockLimit - 4,
        faker->chainId(), faker->groupId());
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None,
        importedTxNum, false, false, true);
    // case8: submit duplicated tx
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::AlreadyInTxPool, importedTxNum);

    // batch import transactions with multiple thread
    auto threadPool = std::make_shared<ThreadPool>("txpoolSubmitter", 8);

    Transactions transactions;
    for (auto i = 0; i < 40; i++)
    {
        auto tmpTx = fakeTransaction(_cryptoSuite, utcTime() + 1000 + i,
            ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
        transactions.push_back(tmpTx);
    }

    tbb::parallel_for(
        tbb::blocked_range<int>(0, transactions.size()), [&](const tbb::blocked_range<int>& _r) {
            for (auto i = _r.begin(); i < _r.end(); i++)
            {
                auto tmpTx = transactions[i];
                checkTxSubmit(txpool, txpoolStorage, tmpTx, tmpTx->hash(),
                    (uint32_t)TransactionStatus::None, 0, false, true, true);
            }
        });
    importedTxNum += transactions.size();
    while (txpoolStorage->size() < importedTxNum)
    {
        std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
        std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "#### txpoolStorage size:" << txpoolStorage->size() << std::endl;
    std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
    // check txs submitted to the ledger
    auto const& txsHash2Data = ledger->txsHashToData();
    for (size_t i = 0; i < transactions.size(); i++)
    {
        while (!txsHash2Data.count(transactions[i]->hash()))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    // case9: the txpool is full
    txpoolConfig->setPoolLimit(importedTxNum);
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::TxPoolIsFull,
        importedTxNum);

    // case10: malformed transaction
    auto encodedData = tx->encode();
    auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
    // fake invalid txData
    for (size_t i = 0; i < txData->size(); i++)
    {
        (*txData)[i] += 100;
    }
    bool verifyFinish = false;
    txpool->asyncSubmit(txData, [&](Error::Ptr _error, TransactionSubmitResult::Ptr _result) {
        BOOST_CHECK(_error->errorCode() == _result->status());
        std::cout << "#### error info:" << _error->errorMessage() << std::endl;
        BOOST_CHECK(_result->txHash() == HashType());
        BOOST_CHECK(_result->status() == (uint32_t)(TransactionStatus::Malform));
        verifyFinish = true;
    });
    while (!verifyFinish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    testAsyncFillBlock(faker, txpool, txpoolStorage, _cryptoSuite);
    testAsyncSealTxs(faker, txpool, txpoolStorage, blockLimit);
}

BOOST_AUTO_TEST_CASE(testTxPoolInitAndSubmitTransaction)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    txPoolInitAndSubmitTransactionTest(false, cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMTxPoolInitAndSubmitTransaction)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signatureImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    txPoolInitAndSubmitTransactionTest(true, cryptoSuite);
}

BOOST_AUTO_TEST_CASE(fillWithSubmit)
{
    // auto hashImpl = std::make_shared<SM3>();
    // auto signatureImpl = std::make_shared<SM2Crypto>();
    // auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    // auto keyPair = signatureImpl->generateKeyPair();
    // std::string groupId = "group_test_for_txpool";
    // std::string chainId = "chain_test_for_txpool";
    // int64_t blockLimit = 10;
    // auto fakeGateWay = std::make_shared<FakeGateWay>();
    // auto faker = std::make_shared<TxPoolFixture>(
    //     keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay);
    // faker->init();

    // // check the txpool config
    // auto txpoolConfig = faker->txpool()->txpoolConfig();
    // BOOST_CHECK(txpoolConfig->txPoolNonceChecker());
    // BOOST_CHECK(txpoolConfig->txValidator());
    // BOOST_CHECK(txpoolConfig->blockFactory());
    // BOOST_CHECK(txpoolConfig->txFactory());
    // BOOST_CHECK(txpoolConfig->ledger());

    // auto txpool = faker->txpool();
    // auto txpoolStorage = txpool->txpoolStorage();

    // // case7: submit success
    // auto tx =
    //     fakeTransaction(cryptoSuite, utcTime() + 2000000, 100, chainId, groupId);

    // tx->encode();
    // auto encodedPtr = std::make_shared<bytes>(tx->takeEncoded());

    // auto hashList = std::make_shared<bcos::crypto::HashList>();
    // hashList->push_back(tx->hash());

    // std::promise<void> fillPromise;
    // txpool->asyncFillBlock(hashList,
    //     [originTx = tx, &fillPromise](Error::Ptr error, bcos::protocol::TransactionsPtr tx) {
    //         BOOST_CHECK(!error);
    //         BOOST_CHECK(tx);
    //         BOOST_CHECK_EQUAL(tx->size(), 1);
    //         BOOST_CHECK_EQUAL((*tx)[0].get(), originTx.get());
    //         fillPromise.set_value();
    //     });
    // fillPromise.get_future().get();
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos