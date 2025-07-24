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
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include "txpool/storage/MemoryStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace std::string_view_literals;

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
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
    std::promise<Error::Ptr> promise;
    _txpool->asyncFillBlock(txsHash, [&promise](Error::Ptr _error, ConstTransactionsPtr) {
        promise.set_value(std::move(_error));
    });
    auto error = promise.get_future().get();
    BOOST_CHECK(error->errorCode() == CommonError::TransactionsMissing);

    // verify block with invalid txsHash
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    std::promise<std::tuple<Error::Ptr, bool>> promise2;
    _txpool->asyncVerifyBlock(
        _faker->nodeID(), block, [&promise2](Error::Ptr _error, bool _result) {
            promise2.set_value({std::move(_error), _result});
        });
    auto [e, r] = promise2.get_future().get();
    BOOST_CHECK(e->errorCode() == CommonError::TransactionsMissing);
    BOOST_CHECK(r == false);

    // case3: with some txs hitted
    auto txHash = _cryptoSuite->hashImpl()->hash("test"sv);
    txsHash->emplace_back(txHash);
    // auto txMetaData = blockFactory->createTransactionMetaData(txHash, txHash.abridged());
    auto txMetaData = _faker->blockFactory()->createTransactionMetaData();
    txMetaData->setHash(txHash);
    txMetaData->setTo(txHash.abridged());
    block->appendTransactionMetaData(txMetaData);

    std::promise<Error::Ptr> promise5;
    _txpool->asyncFillBlock(
        txsHash, [&promise5](Error::Ptr _error, auto&&) { promise5.set_value(std::move(_error)); });
    e = promise5.get_future().get();
    BOOST_CHECK(e->errorCode() == CommonError::TransactionsMissing);

    blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    std::cout << "#### test case3" << std::endl;

    std::promise<std::tuple<Error::Ptr, bool>> promise6;
    _txpool->asyncVerifyBlock(_faker->nodeID(), block,
        [&](Error::Ptr _error, bool _result) { promise6.set_value({std::move(_error), _result}); });
    std::tie(e, r) = promise6.get_future().get();
    BOOST_CHECK(e->errorCode() == CommonError::TransactionsMissing);
    BOOST_CHECK(r == false);

    // case4: duplicate tx in block, and tx in txpool, verify failed
    auto tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime()));
    blockHeader->setNumber(100);
    blockHeader->calculateHash(*_cryptoSuite->hashImpl());
    tx->setBatchId(blockHeader->number() - 1);
    tx->setBatchHash(blockHeader->hash());
    _txpoolStorage->insert(tx);
    _txpoolStorage->batchMarkTxs(
        {tx->hash()}, blockHeader->number() - 1, blockHeader->hash(), true);
    txMetaData = _faker->blockFactory()->createTransactionMetaData();
    txMetaData->setHash(tx->hash());
    txMetaData->setTo(std::string(tx->to()));
    block->appendTransactionMetaData(txMetaData);
    bcos::bytes data;
    block->encode(data);
    std::promise<std::tuple<Error::Ptr, bool>> promise7;
    _txpool->asyncVerifyBlock(_faker->nodeID(), block,
        [&](Error::Ptr _error, bool _result) { promise7.set_value({std::move(_error), _result}); });
    std::tie(e, r) = promise7.get_future().get();
    // FIXME: duplicate tx in block, verify failed
    BOOST_CHECK(e->errorCode() == CommonError::VerifyProposalFailed);
    BOOST_CHECK(r == false);

    dynamic_cast<MemoryStorage&>(*_txpoolStorage).remove(tx->hash());
}

void testAsyncSealTxs(TxPoolFixture::Ptr _faker, TxPoolInterface::Ptr _txpool,
    TxPoolStorageInterface::Ptr _txpoolStorage, int64_t _blockLimit, CryptoSuite::Ptr _cryptoSuite,
    std::function<void()> cleanWeb3Func = nullptr)
{
    // asyncSealTxs
    auto originTxsSize = _txpoolStorage->size();
    size_t txsLimit = 10;
    HashListPtr sealedTxs = std::make_shared<HashList>();
    // seal 10 txs
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(txsLimit, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == txsLimit);
        for (size_t i = 0; i < _txsMetaDataList->transactionsMetaDataSize(); i++)
        {
            sealedTxs->emplace_back(_txsMetaDataList->transactionHash(i));
        }
        BOOST_CHECK(_txpoolStorage->size() == originTxsSize);
    }
    // seal again to fetch all unsealed txs
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == (originTxsSize - txsLimit));
        BOOST_CHECK(_txpoolStorage->size() == originTxsSize);
        std::set<HashType> txsSet(sealedTxs->begin(), sealedTxs->end());
        for (size_t i = 0; i < _txsMetaDataList->transactionsMetaDataSize(); i++)
        {
            auto const& hash = _txsMetaDataList->transactionHash(i);
            BOOST_CHECK(!txsSet.contains(hash));
        }
    }

    // unseal 10 txs
    {
        std::promise<void> promise;
        _txpool->asyncMarkTxs(*sealedTxs, false, -1, HashType(), [&](Error::Ptr _error) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_txpoolStorage->size() == originTxsSize);
            promise.set_value();
        });
        promise.get_future().get();
    }

    // seal again
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == sealedTxs->size());
        BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == sealedTxs->size());
    }
    auto blockHash = _cryptoSuite->hashImpl()->hash("blockHash"sv);
    auto blockNumber = 10;
    // mark txs to given proposal as false, expect: mark failed
    {
        std::promise<void> promise;
        _txpool->asyncMarkTxs(*sealedTxs, false, blockNumber, blockHash, [&](Error::Ptr _error) {
            BOOST_CHECK(_error == nullptr);
            promise.set_value();
        });
        promise.get_future().get();
    }

    // re-seal
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        auto size = _txsMetaDataList->transactionsMetaDataSize();
        BOOST_CHECK_EQUAL(size, 0);
        BOOST_CHECK_EQUAL(_txsMetaDataList->transactionsHashSize(), 0);
    }

    // mark txs to as false, expect mark success
    {
        std::promise<void> promise;

        _txpool->asyncMarkTxs(*sealedTxs, false, -1, HashType(), [&](Error::Ptr _error) {
            BOOST_CHECK(_error == nullptr);
            promise.set_value();
        });
        promise.get_future().get();
    }
    // re-seal success
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == sealedTxs->size());
        BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == sealedTxs->size());
    }

    // mark txs to given proposal as true
    {
        std::promise<void> promise;

        _txpool->asyncMarkTxs(*sealedTxs, true, blockNumber, blockHash, [&](Error::Ptr _error) {
            BOOST_CHECK(_error == nullptr);
            promise.set_value();
        });
        promise.get_future().get();
    }

    // reseal failed
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == 0);
        BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == 0);
    }

    // mark txs to given proposal as false, expect success
    {
        std::promise<void> promise;

        _txpool->asyncMarkTxs(*sealedTxs, false, blockNumber, blockHash, [&](Error::Ptr _error) {
            BOOST_CHECK(_error == nullptr);
            promise.set_value();
        });
        promise.get_future().get();
    }

    // re-seal success
    {
        auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
        BOOST_CHECK(_txsMetaDataList->transactionsMetaDataSize() == sealedTxs->size());
        BOOST_CHECK(_txsMetaDataList->transactionsHashSize() == sealedTxs->size());
    }

    // test asyncNotifyBlockResult
    blockNumber = _faker->ledger()->blockNumber() + _blockLimit;
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

    auto finish = false;
    _faker->asyncNotifyBlockResult(blockNumber, txsResult, [&](Error::Ptr _error) {
        BOOST_CHECK(_error == nullptr);
        finish = true;
    });
    auto startT = utcTime();
    while ((!finish || (_txpoolStorage->size() != originTxsSize - sealedTxs->size())) &&
           (utcTime() - startT <= 10 * 1000))
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
    for (auto tx : *notifiedTxs | RANGES::views::filter([](auto const& tx) {
             return tx->type() != static_cast<uint8_t>(TransactionType::Web3Transaction);
         }))
    {
        BOOST_CHECK(txPoolNonceChecker->checkNonce(*tx) == TransactionStatus::None);
        BOOST_CHECK(ledgerNonceChecker->checkNonce(*tx) == TransactionStatus::NonceCheckFail);
    }
    // check the nonce of ledger->blockNumber() hash been removed from ledgerNonceChecker
    auto const& blockData = _faker->ledger()->ledgerData();
    auto const& nonceList = blockData[_faker->ledger()->blockNumber()]->nonces();
    for (auto const& nonce : *nonceList)
    {
        BOOST_CHECK(ledgerNonceChecker->exists(nonce) == false);
    }

    // case: the other left txs expired for invalid blockLimit
    std::cout << "######### asyncSealTxs with invalid blocklimit" << std::endl;
    std::cout << "##### origin txsSize:" << _txpoolStorage->size() << std::endl;

    if (cleanWeb3Func)
    {
        cleanWeb3Func();
    }
    _txpool->asyncResetTxPool(nullptr);
    auto [_txsMetaDataList, _] = _txpool->sealTxs(100000, nullptr);
    BOOST_CHECK_EQUAL(_txsMetaDataList->transactionsMetaDataSize(), 0);
    BOOST_CHECK_EQUAL(_txsMetaDataList->transactionsHashSize(), 0);
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
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), _cryptoSuite, groupId,
        chainId, blockLimit, fakeGateWay, false, false);
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
    auto tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime()));

    checkTxSubmit(txpool, txpoolStorage, tx, HashType(),
        (uint32_t)TransactionStatus::RequestNotBelongToTheGroup, 0);

    // case2: transaction with invalid blockLimit
    faker->appendSealer(faker->nodeID());
    auto ledger = faker->ledger();
    tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 11000),
        ledger->blockNumber() + blockLimit + 1, faker->chainId(), faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::BlockLimitCheckFail, 0);

    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto const& blockData = ledger->ledgerData();
    auto duplicatedNonce =
        blockData[ledger->blockNumber() - blockLimit + 1]->transaction(0)->nonce();
    tx = fakeTransaction(_cryptoSuite, std::string(duplicatedNonce),
        ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::NonceCheckFail, 0);

    // case4: invalid groupId
    tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime()),
        ledger->blockNumber() + blockLimit - 4, faker->chainId(), "invalidGroup");
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidGroupId, 0);

    // case5: invalid chainId
    tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime()),
        ledger->blockNumber() + blockLimit - 4, "invalidChainId", faker->groupId());
    checkTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::InvalidChainId, 0);

    // case6: invalid signature
    tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 100000),
        ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
    auto pbTx = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    bcos::crypto::KeyPairInterface::Ptr invalidKeyPair = signatureImpl->generateKeyPair();
    auto invalidHash = hashImpl->hash(std::string("test"));
    auto signatureData = signatureImpl->sign(*invalidKeyPair, invalidHash, true);
    pbTx->setSignatureData(*signatureData);
    pbTx->forceSender(bcos::bytes());
    size_t importedTxNum = 0;
    if (!_sm)
    {
        importedTxNum++;
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(), (uint32_t)TransactionStatus::None,
            importedTxNum, false, true, true);
    }
    else
    {
        checkTxSubmit(txpool, txpoolStorage, pbTx, pbTx->hash(),
            (uint32_t)TransactionStatus::InvalidSignature, importedTxNum);
    }

    // case7: submit success
    importedTxNum++;
    tx = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 2000000),
        ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None,
        importedTxNum, false, true, true);
    // case8: submit duplicated tx
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::AlreadyInTxPool, importedTxNum);

    // batch import transactions with multiple thread
    auto threadPool = std::make_shared<ThreadPool>("txpoolSubmitter", 8);

    Transactions transactions;
    for (auto i = 0; i < 40; i++)
    {
        auto tmpTx = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 1000 + i),
            ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
        transactions.push_back(tmpTx);
    }

    for (size_t i = 0; i < transactions.size(); i++)
    {
        auto tmpTx = transactions[i];
        checkTxSubmit(txpool, txpoolStorage, tmpTx, tmpTx->hash(),
            (uint32_t)TransactionStatus::None, 0, false, true, true);
    }
    importedTxNum += transactions.size();
    auto startT = utcTime();
    while ((txpoolStorage->size() < importedTxNum) && (utcTime() - startT <= 10000))
    {
        std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
        std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "#### txpoolStorage size:" << txpoolStorage->size() << std::endl;
    std::cout << "#### importedTxNum:" << importedTxNum << std::endl;

    // check txs submitted to the ledger

    // TxPool doesn't commit any data before block commited
    // auto txsHash2Data = ledger->txsHashToData();
    // for (size_t i = 0; i < transactions.size(); i++)
    // {
    //     auto startT = utcTime();
    //     while (!txsHash2Data.count(transactions[i]->hash()) && (utcTime() - startT <= 5000))
    //     {
    //         std::this_thread::sleep_for(std::chrono::milliseconds(2));
    //         txsHash2Data = ledger->txsHashToData();
    //     }
    //     std::cout << "### txsHash2Data size: " << txsHash2Data.size() << ", i: " << i
    //               << ", transactions size:" << transactions.size() << std::endl;
    // }

    // case9: the txpool is full
    txpoolConfig->setPoolLimit(importedTxNum);
    checkTxSubmit(txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::TxPoolIsFull,
        importedTxNum);

    // case10: malformed transaction
    bcos::bytes encodedData;
    tx->encode(encodedData);
    auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
    // fake invalid txData
    for (size_t i = 0; i < txData->size(); i++)
    {
        (*txData)[i] += 100;
    }
    try
    {
        auto _result = bcos::task::syncWait(txpool->submitTransaction(tx, true));
    }
    catch (bcos::Error& e)
    {
        // TODO: Put TransactionStatus::Malformed into bcos::Error
        // BOOST_CHECK(e.errorCode() == _result->status());
        std::cout << "#### error info:" << e.errorMessage() << std::endl;
        // BOOST_CHECK(_result->txHash() == HashType());
        // BOOST_CHECK(_result->status() == (uint32_t)(TransactionStatus::Malform));
    }
    std::cout << "#### testAsyncFillBlock" << std::endl;
    testAsyncFillBlock(faker, txpool, txpoolStorage, _cryptoSuite);
    std::cout << "#### testAsyncSealTxs" << std::endl;
    testAsyncSealTxs(faker, txpool, txpoolStorage, blockLimit, _cryptoSuite);
    // clear all the txs before exit
    txpool->txpoolStorage()->clear();
    std::cout << "#### txPoolInitAndSubmitTransactionTest finish" << std::endl;
}

void txPoolInitAndSubmitWeb3TransactionTest(CryptoSuite::Ptr _cryptoSuite, bool multiTx = false)
{
    auto signatureImpl = _cryptoSuite->signatureImpl();
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test_for_txpool";
    std::string chainId = "chain_test_for_txpool";
    int64_t blockLimit = 10;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(keyPair->publicKey(), _cryptoSuite, groupId,
        chainId, blockLimit, fakeGateWay, false, false);
    faker->init();

    auto txpoolConfig = faker->txpool()->txpoolConfig();
    auto txpool = faker->txpool();
    auto txpoolStorage = txpool->txpoolStorage();
    auto ledger = faker->ledger();

    auto const eoaKey = _cryptoSuite->signatureImpl()->generateKeyPair();
    // case3: transaction with invalid nonce(conflict with the ledger nonce)
    auto const& blockData = ledger->ledgerData();
    size_t importedTxNum = 1;

    auto duplicatedNonce =
        blockData[ledger->blockNumber() - blockLimit + 1]->transaction(0)->nonce();
    auto tx = fakeWeb3Tx(_cryptoSuite, std::string(duplicatedNonce), eoaKey);
    // bcos nonce not effect web3 nonce
    checkWebTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None, importedTxNum);

    u256 fakeNonce = u256(duplicatedNonce);
    StorageState state{.nonce = fakeNonce.convert_to<std::string>(), .balance = "1"};
    faker->ledger()->setStorageState(
        eoaKey->address(_cryptoSuite->hashImpl()).hex(), std::move(state));
    // ledger update to fakeNonce, tx do not clean
    std::this_thread::sleep_for(std::chrono::milliseconds(TXPOOL_CLEANUP_TIME));
    BOOST_CHECK_EQUAL(txpoolStorage->size(), importedTxNum);

    // new nonce, submit success
    ++fakeNonce;
    tx = fakeWeb3Tx(_cryptoSuite, fakeNonce.convert_to<std::string>(), eoaKey);
    importedTxNum++;
    checkWebTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None, importedTxNum);
    // duplicated tx, submit failed
    auto duplicate_tx = fakeWeb3Tx(_cryptoSuite, fakeNonce.convert_to<std::string>(), eoaKey);
    checkWebTxSubmit(txpool, txpoolStorage, duplicate_tx, duplicate_tx->hash(),
        (uint32_t)TransactionStatus::NonceCheckFail, importedTxNum);

    ++fakeNonce;
    // case7: submit success
    importedTxNum++;
    tx = fakeWeb3Tx(_cryptoSuite, fakeNonce.convert_to<std::string>(), eoaKey);
    checkWebTxSubmit(
        txpool, txpoolStorage, tx, tx->hash(), (uint32_t)TransactionStatus::None, importedTxNum);
    // case8: submit duplicated tx
    checkWebTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::AlreadyInTxPool, importedTxNum);

    // too big nonce
    tx = fakeWeb3Tx(_cryptoSuite,
        (fakeNonce + DEFAULT_WEB3_NONCE_CHECK_LIMIT + 1).convert_to<std::string>(), eoaKey);
    checkWebTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::NonceCheckFail, importedTxNum);
    // batch import transactions

    Transactions transactions;
    for (auto i = 0; i < 40; i++)
    {
        auto tmpTx = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 1000 + i),
            ledger->blockNumber() + blockLimit - 4, faker->chainId(), faker->groupId());
        transactions.push_back(tmpTx);
    }

    ++fakeNonce;
    for (auto i = 0; i < 40; i++)
    {
        auto tmpTx = fakeWeb3Tx(_cryptoSuite, (fakeNonce + i).convert_to<std::string>(), eoaKey);
        transactions.push_back(tmpTx);
    }

    for (size_t i = 0; i < transactions.size(); i++)
    {
        auto tmpTx = transactions[i];
        checkWebTxSubmit(txpool, txpoolStorage, tmpTx, tmpTx->hash(),
            (uint32_t)TransactionStatus::None, ++importedTxNum);
    }
    auto startT = utcTime();
    while ((txpoolStorage->size() < importedTxNum) && (utcTime() - startT <= 10000))
    {
        std::cout << "#### txpoolStorage->size:" << txpoolStorage->size() << std::endl;
        std::cout << "#### importedTxNum:" << importedTxNum << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "#### txpoolStorage size:" << txpoolStorage->size() << std::endl;
    std::cout << "#### importedTxNum:" << importedTxNum << std::endl;

    // case9: the txpool is full
    txpoolConfig->setPoolLimit(importedTxNum);
    checkWebTxSubmit(txpool, txpoolStorage, tx, tx->hash(),
        (uint32_t)TransactionStatus::TxPoolIsFull, importedTxNum);

    // case10: malformed transaction
    bcos::bytes encodedData;
    tx->encode(encodedData);
    auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
    // fake invalid txData
    for (size_t i = 0; i < txData->size(); i++)
    {
        (*txData)[i] += 100;
    }
    try
    {
        bcos::task::syncWait(txpool->submitTransaction(tx, false));
    }
    catch (bcos::Error& e)
    {
        // TODO: Put TransactionStatus::Malformed into bcos::Error
        // BOOST_CHECK(e.errorCode() == _result->status());
        std::cout << "#### error info:" << e.errorMessage() << std::endl;
        // BOOST_CHECK(_result->txHash() == HashType());
        // BOOST_CHECK(_result->status() == (uint32_t)(TransactionStatus::Malform));
    }
    std::cout << "#### testAsyncFillBlock" << std::endl;
    testAsyncFillBlock(faker, txpool, txpoolStorage, _cryptoSuite);
    std::cout << "#### testAsyncSealTxs" << std::endl;
    testAsyncSealTxs(faker, txpool, txpoolStorage, blockLimit, _cryptoSuite, [&]() {
        txpool->txpoolConfig()->txValidator()->web3NonceChecker()->insert(
            std::string(tx->sender()), u256("0xffffffffffffffffffffffffff"));
    });
    // clear all the txs before exit
    txpool->txpoolStorage()->clear();
    std::cout << "#### txPoolInitAndSubmitTransactionTest finish" << std::endl;
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

BOOST_AUTO_TEST_CASE(testWeb3Tx)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    txPoolInitAndSubmitWeb3TransactionTest(cryptoSuite);
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
    //     fakeTransaction(cryptoSuite, utcTime() + 2000000, std::to_string(100), chainId, groupId);

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