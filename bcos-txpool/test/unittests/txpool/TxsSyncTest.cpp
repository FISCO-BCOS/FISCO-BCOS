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
 * @brief unit test for txs-fetching related logic of txpool
 * @file TxsSyncTest.cpp.cpp
 * @author: yujiechen
 * @date 2021-05-26
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <tbb/blocked_range.h>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <exception>

using namespace bcos::txpool;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(txsSyncTest, TestPromptFixture)

Transactions importTransactions(
    size_t _txsNum, CryptoSuite::Ptr _cryptoSuite, const TxPoolFixture::Ptr& _faker)
{
    auto txpool = _faker->txpool();
    auto ledger = _faker->ledger();
    Transactions transactions;
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto transaction = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 1000 + i),
            ledger->blockNumber() + 1, _faker->chainId(), _faker->groupId());
        transactions.push_back(transaction);
        task::syncWait(txpool->broadcastTransaction(*transaction));
        task::wait(txpool->submitTransaction(transaction));
    }
    auto startT = utcTime();
    while (txpool->txpoolStorage()->size() < _txsNum && (utcTime() - startT <= 10000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return transactions;
}

void importTransactionsNew(
    size_t _txsNum, CryptoSuite::Ptr _cryptoSuite, const TxPoolFixture::Ptr& _faker)
{
    auto txpool = _faker->txpool();
    auto ledger = _faker->ledger();
    Transactions transactions;
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto transaction = fakeTransaction(_cryptoSuite, std::to_string(utcTime() + 10000 + i),
            ledger->blockNumber() + 1, _faker->chainId(), _faker->groupId());
        task::syncWait(txpool->broadcastTransaction(*transaction));
        transactions.push_back(transaction);
    }

    // Test parallel submit
    tbb::parallel_for(tbb::blocked_range<size_t>(0, transactions.size()), [&txpool, &transactions](
                                                                              auto& range) {
        for (auto i = range.begin(); i < range.end(); ++i)
        {
            auto& transaction = transactions[i];
            task::wait(
                [](decltype(txpool) txpool, decltype(transaction) transaction) -> task::Task<void> {
                    auto result = co_await txpool->submitTransaction(transaction);
                    BOOST_CHECK_EQUAL(result->status(), 0);
                    TXPOOL_LOG(DEBUG) << "Submit transaction successed";
                }(txpool, transaction));
        }
    });

    auto startT = utcTime();
    while (txpool->txpoolStorage()->size() < _txsNum && (utcTime() - startT <= 10000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void testTransactionSync(bool _onlyTxsStatus = false)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    std::string groupId = "test-group";
    std::string chainId = "test-chain";
    int64_t blockLimit = 15;
    auto fakeGateWay = std::make_shared<FakeGateWay>();
    auto faker = std::make_shared<TxPoolFixture>(
        keyPair->publicKey(), cryptoSuite, groupId, chainId, blockLimit, fakeGateWay, false, false);
    if (_onlyTxsStatus)
    {
        faker->resetToFakeTransactionSync();
    }
    faker->appendSealer(keyPair->publicKey());
    // init the config
    faker->init();
    auto txpool = faker->txpool();
    auto ledger = faker->ledger();
    // append sessions
    size_t sessionSize = 8;
    std::vector<TxPoolFixture::Ptr> txpoolPeerList;
    std::vector<NodeIDPtr> sessionNodeIDs;
    sessionNodeIDs.emplace_back(keyPair->publicKey());
    for (size_t i = 0; i < sessionSize; i++)
    {
        auto nodeId = signatureImpl->generateKeyPair()->publicKey();
        auto sessionFaker = std::make_shared<TxPoolFixture>(
            nodeId, cryptoSuite, groupId, chainId, blockLimit, fakeGateWay, false, false);
        sessionFaker->init();
        if (_onlyTxsStatus)
        {
            sessionFaker->resetToFakeTransactionSync();
        }
        sessionNodeIDs.emplace_back(nodeId);
        faker->appendSealer(nodeId);
        txpoolPeerList.push_back(sessionFaker);
    }
    for (auto& sessionFaker : txpoolPeerList)
    {
        for (auto const& nodeID : sessionNodeIDs)
        {
            // make sure the session in the group
            sessionFaker->appendSealer(nodeID);
        }
    }
    size_t txsNum = 10;
    auto transactions = importTransactions(txsNum, cryptoSuite, faker);

    // Assume transaction had been received

    // check maintain transactions
    if (_onlyTxsStatus)
    {
        for (auto txpoolPeer : txpoolPeerList)
        {
            // all the peers has received the txsStatus, and fetch txs from other peers
            BOOST_CHECK(faker->frontService()->getAsyncSendSizeByNodeID(txpoolPeer->nodeID()) >= 1);
            auto startT = utcTime();
            while (txpoolPeer->txpool()->txpoolStorage()->size() < txsNum &&
                   (utcTime() - startT <= 10000))
            {
                // maintain the downloading txs
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::cout << "### txpoolSize: " << txpoolPeer->txpool()->txpoolStorage()->size()
                      << ", txsNum:" << txsNum << ", peer: " << txpoolPeer->nodeID()->shortHex()
                      << std::endl;
            BOOST_CHECK(txpoolPeer->txpool()->txpoolStorage()->size() == txsNum);
        }
        // maintain transactions again
        auto originSendSize = faker->frontService()->totalSendMsgSize();
        BOOST_CHECK(faker->frontService()->totalSendMsgSize() == originSendSize);
        return;
    }
    // check the transactions has been broadcasted to all the node
    // maintainDownloadingTransactions and check the size
    for (auto txpoolPeer : txpoolPeerList)
    {
        BOOST_CHECK(faker->frontService()->getAsyncSendSizeByNodeID(txpoolPeer->nodeID()) >= 1);
        auto startT = utcTime();
        while (
            txpoolPeer->txpool()->txpoolStorage()->size() < txsNum && (utcTime() - startT <= 10000))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        BOOST_CHECK(txpoolPeer->txpool()->txpoolStorage()->size() == txsNum);
    }
    // +1 for include the node self
    auto forwardSize =
        ((txpoolPeerList.size() + 1) * faker->sync()->config()->forwardPercent() + 99) / 100;

    // with requestMissedTxs request
    // auto maxSendSize = txpoolPeerList.size() + forwardSize + forwardSize;
    // each peer broadcast 10 transactions
    auto maxSendSize = 80;
    BOOST_CHECK_LE(faker->frontService()->totalSendMsgSize(), maxSendSize);
    auto originSendSize = faker->frontService()->totalSendMsgSize();
    std::cout << "##### totalSendMsgSize: " << faker->frontService()->totalSendMsgSize()
              << std::endl;
    std::cout << "#### txpoolPeerList size:" << txpoolPeerList.size() << std::endl;
    std::cout << "##### forwardSize:" << forwardSize << std::endl;
    BOOST_CHECK(faker->frontService()->totalSendMsgSize() == originSendSize);

    // test forward txs status
    auto syncPeer = txpoolPeerList[0];
    // update connected node list
    originSendSize = syncPeer->frontService()->totalSendMsgSize();
    std::cout << "#### before maintainTransactions totalSendMsgSize: " << originSendSize
              << std::endl;
    for (auto txpoolPeer : txpoolPeerList)
    {
        syncPeer->appendSealer(txpoolPeer->nodeID());
    }
    std::cout << "#### after maintainTransactions totalSendMsgSize: "
              << syncPeer->frontService()->totalSendMsgSize() << std::endl;

    // import new transaction to the syncPeer, but not broadcast the imported transaction
    std::cout << "###### test fetch and verify block" << std::endl;
    auto newTxsSize = 1000;
    importTransactionsNew(newTxsSize, cryptoSuite, syncPeer);  // Use new method to import
                                                               // transaction
    // the syncPeer sealTxs
    HashListPtr txsHash = std::make_shared<HashList>();
    bool finish = false;
    syncPeer->txpool()->asyncSealTxs(
        100000, nullptr, [&](Error::Ptr _error, Block::Ptr _fetchedTxs, Block::Ptr) {
            BOOST_CHECK(_error == nullptr);
            // BOOST_CHECK(_fetchedTxs->size() == syncPeer->txpool()->txpoolStorage()->size());
            for (size_t i = 0; i < _fetchedTxs->transactionsMetaDataSize(); i++)
            {
                txsHash->emplace_back(_fetchedTxs->transactionHash(i));
            }
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // assume the faker verify the syncPeer generated proposal
    auto blockFactory = faker->txpool()->txpoolConfig()->blockFactory();
    auto block = blockFactory->createBlock();
    for (auto const& txHash : *txsHash)
    {
        // auto txMetaData = blockFactory->createTransactionMetaData(txHash, txHash.abridged());
        auto txMetaData = blockFactory->createTransactionMetaData();
        txMetaData->setHash(txHash);
        txMetaData->setTo(txHash.abridged());

        block->appendTransactionMetaData(txMetaData);
    }
    auto encodedData = std::make_shared<bytes>();
    block->blockHeader()->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
    block->encode(*encodedData);
    finish = false;
    faker->txpool()->asyncVerifyBlock(
        syncPeer->nodeID(), ref(*encodedData), [&](Error::Ptr _error, bool _result) {
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK(_result == true);
            finish = true;
        });
    while (!finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

BOOST_AUTO_TEST_CASE(testMatainTransactions)
{
    testTransactionSync(false);
}

BOOST_AUTO_TEST_CASE(testOnPeerTxsStatus)
{
    testTransactionSync(true);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
