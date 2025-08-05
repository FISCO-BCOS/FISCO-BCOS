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
 * @brief unit tests for PBFTConfig
 * @file PBFTConfigTest.cpp
 * @author: yujiechen
 * @date 2021-05-28
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"

#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBFTConfigTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testPBFTInit)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = signatureImpl->generateKeyPair();
    auto gateWay = std::make_shared<FakeGateWay>();

    size_t consensusTimeout = 4;
    size_t txCountLimit = 2000;
    auto faker = std::make_shared<PBFTFixture>(cryptoSuite, keyPair, nullptr, txCountLimit);
    faker->frontService()->setGateWay(gateWay);

    // case1: with zero consensus node
    BOOST_CHECK_THROW(faker->init(), InitConsensusException);

    // case2: with 10 consensus nodes
    auto pbftConfig = faker->pbftConfig();
    auto ledgerConfig = faker->ledger()->ledgerConfig();
    auto pbftEngine = faker->pbftEngine();

    auto proposalIndex = ledgerConfig->blockNumber() + 1;
    auto parent = (faker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto block = faker->ledger()->init(parent->blockHeader(), true, proposalIndex, 0, 0);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    faker->appendConsensusNode(faker->nodeID());

    // case3: with expired state
    auto pbftMsgFixture = std::make_shared<PBFTMessageFixture>(cryptoSuite, keyPair);
    auto fakedProposal = pbftMsgFixture->fakePBFTProposal(faker->ledger()->blockNumber() - 1,
        ledgerConfig->hash(), *blockData, std::vector<int64_t>(), std::vector<bytes>());
    pbftConfig->storage()->asyncCommitProposal(fakedProposal);
    faker->init();

    BOOST_TEST(pbftConfig->progressedIndex() == faker->ledger()->blockNumber() + 1);
    auto cacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(pbftEngine->cacheProcessor());
    BOOST_TEST(cacheProcessor->stableCheckPointQueueSize() == 0);

    fakedProposal = pbftMsgFixture->fakePBFTProposal(faker->ledger()->blockNumber(),
        ledgerConfig->hash(), *blockData, std::vector<int64_t>(), std::vector<bytes>());
    pbftConfig->storage()->asyncCommitProposal(fakedProposal);
    faker->init();

    BOOST_TEST(pbftConfig->progressedIndex() == faker->ledger()->blockNumber() + 1);
    BOOST_TEST(cacheProcessor->stableCheckPointQueueSize() == 0);

    faker->init();
    BOOST_TEST(pbftConfig->nodeIndex() == 0);
    BOOST_TEST(pbftConfig->getConsensusNodeByIndex(0)->nodeID->data() == faker->nodeID()->data());

    // check nodeIndex
    size_t consensusNodesSize = 9;
    for (size_t i = 0; i < consensusNodesSize; i++)
    {
        auto peerKeyPair = signatureImpl->generateKeyPair();
        faker->appendConsensusNode(peerKeyPair->publicKey());
        faker->init();
        auto nodeIndex = pbftConfig->nodeIndex();
        auto node = pbftConfig->getConsensusNodeByIndex(nodeIndex);
        BOOST_TEST(node->nodeID->data() == faker->nodeID()->data());
    }
    BOOST_TEST(pbftConfig->consensusNodeList().size() == (consensusNodesSize + 1));
    BOOST_TEST(pbftConfig->nodeID()->data() == faker->nodeID()->data());

    // check params
    BOOST_TEST(pbftConfig->isConsensusNode());
    pbftConfig->setConsensusTimeout(consensusTimeout);
    BOOST_TEST(pbftConfig->consensusTimeout() == consensusTimeout);
    BOOST_CHECK_EQUAL(pbftConfig->blockTxCountLimit(), txCountLimit);
    // Note: should update this check if consensusNodesSize has been changed
    BOOST_TEST(pbftConfig->minRequiredQuorum() == 7);
    BOOST_TEST(pbftConfig->committedProposal()->index() == faker->ledger()->blockNumber());
    BOOST_TEST(pbftConfig->committedProposal()->hash() == ledgerConfig->hash());
    // check PBFT related information
    BOOST_TEST(pbftConfig->progressedIndex() == faker->ledger()->blockNumber() + 1);
    BOOST_TEST(pbftConfig->view() == 0);
    BOOST_TEST(pbftConfig->toView() == 0);
    // check object
    BOOST_TEST(pbftConfig->cryptoSuite());
    BOOST_TEST(pbftConfig->pbftMessageFactory());
    BOOST_TEST(pbftConfig->frontService());
    BOOST_TEST(pbftConfig->codec());
    BOOST_TEST(pbftConfig->validator());
    BOOST_TEST(pbftConfig->storage());
    BOOST_TEST(pbftConfig->highWaterMark() ==
                pbftConfig->progressedIndex() + pbftConfig->waterMarkLimit());
    BOOST_TEST(pbftConfig->stateMachine());
    BOOST_TEST(pbftConfig->expectedCheckPoint() == faker->ledger()->blockNumber() + 1);


#if 0
    // case4: with new committed index, but invalid data
    auto hash = hashImpl->hash(std::string("checkpoint"));
    fakedProposal = pbftMsgFixture->fakePBFTProposal(faker->ledger()->blockNumber() + 1, hash,
        bytes(), std::vector<int64_t>(), std::vector<bytes>());
    pbftConfig->storage()->asyncCommitProposal(fakedProposal);
    faker->init();
    BOOST_TEST(pbftConfig->progressedIndex() == faker->ledger()->blockNumber() + 1);
    BOOST_TEST(pbftEngine->cacheProcessor()->committedQueue() == 0);
    BOOST_TEST(pbftConfig->expectedCheckPoint() == faker->ledger()->blockNumber() + 1);
#endif

    // case5: with new committed index and valid data, collect enough checkpoint proposal and commit
    // it
    std::cout << "##### case5: with new committed index and valid data" << std::endl;
    faker->clearConsensusNodeList();
    faker->appendConsensusNode(faker->nodeID());
    auto blockHeader = block->blockHeader();
    fakedProposal = pbftMsgFixture->fakePBFTProposal(proposalIndex, blockHeader->hash(), *blockData,
        std::vector<int64_t>(), std::vector<bytes>());
    pbftConfig->storage()->asyncCommitProposal(fakedProposal);
    faker->init();
    BOOST_TEST(pbftConfig->minRequiredQuorum() == 1);
    auto startT = utcTime();
    while (pbftConfig->committedProposal()->index() != proposalIndex &&
           (utcTime() - startT <= 60 * 1000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    BOOST_TEST(faker->ledger()->blockNumber() == proposalIndex);
    BOOST_TEST(pbftConfig->progressedIndex() == proposalIndex + 1);
    BOOST_TEST(cacheProcessor->committedQueueSize() == 0);
    BOOST_TEST(cacheProcessor->stableCheckPointQueueSize() == 0);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
