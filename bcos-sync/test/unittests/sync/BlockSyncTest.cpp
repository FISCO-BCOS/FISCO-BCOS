/**
 *  Copyright (C) 2021 bcos-sync.
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
 * @brief test for the BlockSync
 * @file BlockSyncTest.h
 * @author: yujiechen
 * @date 2021-06-08
 */
#include "SyncFixture.h"
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(BlockSyncTest, TestPromptFixture)
void testRequestAndDownloadBlock(CryptoSuite::Ptr _cryptoSuite)
{
    auto gateWay = std::make_shared<FakeGateWay>();
    BlockNumber maxBlock = 10;
    auto newerPeer = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, (maxBlock + 1));
    auto ledgerData = newerPeer->ledger()->ledgerData();
    auto latestHash = (ledgerData[newerPeer->ledger()->blockNumber()])->blockHeader()->hash();

    BlockNumber minBlock = 5;
    auto lowerPeer = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, (minBlock + 1));
    std::vector<NodeIDPtr> nodeList;
    nodeList.push_back(newerPeer->nodeID());
    nodeList.push_back(lowerPeer->nodeID());
    newerPeer->setObservers(nodeList);
    lowerPeer->setObservers(nodeList);

    newerPeer->init();
    lowerPeer->init();

    // maintainPeersConnection
    newerPeer->sync()->executeWorker();
    lowerPeer->sync()->executeWorker();
    while (!newerPeer->sync()->syncStatus()->hasPeer(lowerPeer->nodeID()) ||
           !lowerPeer->sync()->syncStatus()->hasPeer(newerPeer->nodeID()))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    BOOST_CHECK(newerPeer->syncConfig()->knownHighestNumber() == maxBlock);
    BOOST_CHECK(lowerPeer->syncConfig()->knownHighestNumber() == maxBlock);
    BOOST_CHECK(lowerPeer->syncConfig()->knownLatestHash().asBytes() == latestHash.asBytes());
    BOOST_CHECK(newerPeer->syncConfig()->knownLatestHash().asBytes() == latestHash.asBytes());
    // check request/response blocks
    while (lowerPeer->ledger()->blockNumber() != maxBlock)
    {
        newerPeer->sync()->executeWorker();
        lowerPeer->sync()->executeWorker();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    BOOST_CHECK(newerPeer->consensus()->ledgerConfig()->blockNumber() == maxBlock);
    BOOST_CHECK(lowerPeer->consensus()->ledgerConfig()->blockNumber() == maxBlock);
}

bool checkPeer(std::vector<SyncFixture::Ptr> const& _peerList, size_t _expectedPeerSize)
{
    for (auto peer : _peerList)
    {
        if (peer->sync()->syncStatus()->peers()->size() < _expectedPeerSize)
        {
            return false;
        }
    }
    return true;
}

bool downloadFinish(
    std::vector<SyncFixture::Ptr> const& _peerList, BlockNumber _expectedBlockNumber)
{
    for (auto peer : _peerList)
    {
        if (peer->ledger()->blockNumber() != _expectedBlockNumber)
        {
            return false;
        }
    }
    return true;
}

void testComplicatedCase(CryptoSuite::Ptr _cryptoSuite)
{
    auto gateWay = std::make_shared<FakeGateWay>();
    std::vector<SyncFixture::Ptr> syncPeerList;
    std::vector<NodeIDPtr> nodeList;

    BlockNumber maxBlockNumber = 50;
    size_t maxBlockNumberPeerSize = 2;

    BlockNumber medianBlockNumber = 20;
    size_t medianBlockNumberPeerSize = 2;

    BlockNumber minBlockNumber = 10;
    size_t minBlockNumberPeerSize = 3;

    for (size_t i = 0; i < maxBlockNumberPeerSize; i++)
    {
        auto faker = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, maxBlockNumber + 1);
        nodeList.push_back(faker->nodeID());
        syncPeerList.push_back(faker);
    }

    for (size_t i = 0; i < medianBlockNumberPeerSize; i++)
    {
        auto faker = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, medianBlockNumber + 1);
        nodeList.push_back(faker->nodeID());
        syncPeerList.push_back(faker);
    }

    for (size_t i = 0; i < minBlockNumberPeerSize; i++)
    {
        auto faker = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, minBlockNumber + 1);
        nodeList.push_back(faker->nodeID());
        syncPeerList.push_back(faker);
    }
    std::vector<bytes> sealers;
    sealers.push_back(_cryptoSuite->signatureImpl()->generateKeyPair()->publicKey()->data());
    // with different genesis hash with others
    auto invalidFaker = std::make_shared<SyncFixture>(_cryptoSuite, gateWay, 0, sealers);
    nodeList.push_back(invalidFaker->nodeID());
    invalidFaker->setObservers(nodeList);
    invalidFaker->init();
    for (auto faker : syncPeerList)
    {
        faker->setObservers(nodeList);
        faker->init();
    }
    // maintainPeersConnection
    for (auto faker : syncPeerList)
    {
        faker->sync()->executeWorker();
    }
    invalidFaker->sync()->executeWorker();

    size_t peerSize = maxBlockNumberPeerSize + medianBlockNumberPeerSize + minBlockNumberPeerSize;
    // check peers
    while (!checkPeer(syncPeerList, peerSize))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto ledgerData = (syncPeerList[0])->ledger()->ledgerData();
    auto latestBlock = ledgerData[(syncPeerList[0])->ledger()->blockNumber()];
    auto genesisBlock = ledgerData[0];
    // check the maxKnownBlockNumber
    for (auto faker : syncPeerList)
    {
        auto genesisBlockHeader = genesisBlock->blockHeader();
        auto latestBlockHeader = latestBlock->blockHeader();
        BOOST_CHECK(faker->syncConfig()->genesisHash() == genesisBlockHeader->hash());
        BOOST_CHECK(faker->syncConfig()->knownLatestHash() == latestBlockHeader->hash());
        BOOST_CHECK(faker->syncConfig()->knownHighestNumber() == maxBlockNumber);
    }
    auto invalidLedgerData = invalidFaker->ledger()->ledgerData();
    auto invalidLatestBlock = invalidLedgerData[invalidFaker->ledger()->blockNumber()];
    auto invalidGenesisBlock = invalidLedgerData[0];
    BOOST_CHECK(invalidFaker->sync()->syncStatus()->peers()->size() == 1);
    auto invalidGenesisBlockHeader = invalidGenesisBlock->blockHeader();
    BOOST_CHECK(invalidFaker->syncConfig()->genesisHash() == invalidGenesisBlockHeader->hash());
    auto invalidLatestBlockHeader = invalidLatestBlock->blockHeader();
    BOOST_CHECK(invalidFaker->syncConfig()->knownLatestHash() == invalidLatestBlockHeader->hash());
    BOOST_CHECK(invalidFaker->syncConfig()->knownHighestNumber() == 0);

    // wait the nodes to sync blocks
    while (!downloadFinish(syncPeerList, maxBlockNumber))
    {
        for (auto faker : syncPeerList)
        {
            faker->sync()->executeWorker();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}


BOOST_AUTO_TEST_CASE(testNonSMRequestAndDownloadBlock)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    testRequestAndDownloadBlock(cryptoSuite);
    testComplicatedCase(cryptoSuite);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos