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
 * @brief unit tests for PBFT viewchange
 * @file PBFTViewChangeTest.cpp
 * @author: yujiechen
 * @date 2021-06-01
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBFTViewChangeTest, TestPromptFixture)
bool shouldExit(std::map<IndexType, PBFTFixture::Ptr>& _fakers, BlockNumber _number)
{
    for (IndexType i = 0; i < _fakers.size(); i++)
    {
        auto faker = _fakers[i];
        if (faker->ledger()->blockNumber() != _number)
        {
            return false;
        }
    }
    return true;
}
BOOST_AUTO_TEST_CASE(testViewChangeWithPrecommitProposals)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    size_t consensusNodeSize = 10;
    size_t _connectedNodes = 10;
    size_t currentBlockNumber = 11;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, _connectedNodes);

    // check the leader notify the sealer to seal proposals
    BlockNumber expectedProposal = (size_t)(fakerMap[0]->ledger()->blockNumber() + 1);
    IndexType leaderIndex = fakerMap[0]->pbftConfig()->leaderIndex(expectedProposal);
    auto leaderFaker = fakerMap[leaderIndex];

    auto pbftMsgFixture = std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedProposal, 10);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    // blockNumber + 3
    BlockNumber futureBlockIndex = expectedProposal + 2;
    IndexType futureLeaderIndex = fakerMap[0]->pbftConfig()->leaderIndex(futureBlockIndex);
    auto futureLeader = fakerMap[futureLeaderIndex];
    auto futureBlock = fakeBlock(cryptoSuite, futureLeader, futureBlockIndex, 10);
    auto futureBlockData = std::make_shared<bytes>();
    futureBlock->encode(*futureBlockData);

    auto blockHeader = block->blockHeader();
    // the leader submit proposal
    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, ref(*blockData), blockHeader->number(), blockHeader->hash(), nullptr);
    // the future leader submit the proposal
    auto futureBlockHeader = futureBlock->blockHeader();
    futureLeader->pbftEngine()->asyncSubmitProposal(false, ref(*futureBlockData),
        futureBlockHeader->number(), futureBlockHeader->hash(), nullptr);

    auto cacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(leaderFaker->pbftEngine()->cacheProcessor());
    BOOST_CHECK(cacheProcessor->caches().size() == 1);
    auto cache =
        std::dynamic_pointer_cast<FakePBFTCache>((cacheProcessor->caches())[expectedProposal]);
    BOOST_CHECK(cache->prePrepare());
    BOOST_CHECK(cache->index() == expectedProposal);
    BOOST_CHECK(cache->prePrepare());

    auto futureCacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(futureLeader->pbftEngine()->cacheProcessor());
    auto futureCache = std::dynamic_pointer_cast<FakePBFTCache>(
        (futureCacheProcessor->caches())[futureBlockIndex]);
    BOOST_CHECK(futureCacheProcessor->caches().size() == 1);
    BOOST_CHECK(futureCache->prePrepare());
    BOOST_CHECK(futureCache->index() == futureBlockIndex);
    BOOST_CHECK(futureCache->prePrepare());

    for (auto const& otherNode : fakerMap)
    {
        otherNode.second->pbftEngine()->executeWorker();
    }
    // assume five nodes into preCommit
    size_t precommitSize = 5;
    for (size_t i = 0; i < std::min(precommitSize, fakerMap.size()); i++)
    {
        auto faker = fakerMap[i];
        FakeCacheProcessor::Ptr cacheProcessor =
            std::dynamic_pointer_cast<FakeCacheProcessor>(faker->pbftEngine()->cacheProcessor());
        BOOST_CHECK(cacheProcessor->caches().size() == 2);
        auto cache =
            std::dynamic_pointer_cast<FakePBFTCache>((cacheProcessor->caches())[expectedProposal]);
        BOOST_CHECK(cache->prePrepare());
        BOOST_CHECK(cache->index() == expectedProposal);
        cache->intoPrecommit();

        auto futureCache =
            std::dynamic_pointer_cast<FakePBFTCache>((cacheProcessor->caches())[futureBlockIndex]);
        BOOST_CHECK(futureCache->prePrepare());
        BOOST_CHECK(futureCache->index() == futureBlockIndex);
        BOOST_CHECK(futureCache->prePrepare());
        futureCache->intoPrecommit();
    }

    for (size_t i = 0; i < fakerMap.size(); i++)
    {
        auto faker = fakerMap[i];
        faker->pbftConfig()->setConsensusTimeout(1000);
    }
    auto startT = utcTime();
    while (!shouldExit(fakerMap, futureBlockIndex) && (utcTime() - startT <= 10 * 3000))
    {
        for (size_t i = 0; i < fakerMap.size(); i++)
        {
            auto faker = fakerMap[i];
            faker->pbftEngine()->executeWorkerByRoundbin();
        }
    }
    // check reach new view
    for (size_t i = 0; i < fakerMap.size(); i++)
    {
        auto faker = fakerMap[i];
        BOOST_CHECK(faker->pbftConfig()->view() > 0);
        BOOST_CHECK(faker->pbftConfig()->toView() == (faker->pbftConfig()->view()));
        BOOST_CHECK(faker->pbftConfig()->timer()->changeCycle() == 0);
        BOOST_CHECK(faker->pbftEngine()->isTimeout() == false);
        BOOST_CHECK(faker->ledger()->blockNumber() == futureBlockIndex);
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos