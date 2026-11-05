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
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"

#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
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
// The cache at _index once it holds its pre-prepare, else nullptr. A cache is created by the
// first message of any kind for that index, so a prepare vote that overtakes the pre-prepare
// leaves a cache with no pre-prepare; find() also keeps a missing index from being inserted
// as a null entry the way operator[] would.
inline std::shared_ptr<FakePBFTCache> preparedCache(
    PBFTCacheProcessor::Ptr const& _processor, BlockNumber _index)
{
    auto& caches = std::dynamic_pointer_cast<FakeCacheProcessor>(_processor)->caches();
    auto it = caches.find(_index);
    if (it == caches.end())
    {
        return nullptr;
    }
    auto cache = std::dynamic_pointer_cast<FakePBFTCache>(it->second);
    return (cache && cache->prePrepare()) ? cache : nullptr;
}

inline std::string cachedIndexes(PBFTCacheProcessor::Ptr const& _processor)
{
    std::string indexes;
    for (auto const& [index, cache] :
        std::dynamic_pointer_cast<FakeCacheProcessor>(_processor)->caches())
    {
        auto fakeCache = std::dynamic_pointer_cast<FakePBFTCache>(cache);
        indexes +=
            std::to_string(index) +
            ((fakeCache && fakeCache->prePrepare()) ? "(pre-prepared) " : "(no pre-prepare) ");
    }
    return indexes;
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

    // The scenario: the first nodes hold both proposals in precommit without having sent their
    // commit votes, so neither proposal can collect a commit quorum (5 of 10 votes) and only a
    // view change can carry them on. The test puts those nodes into precommit by hand, which
    // needs their pre-prepares. Hold them from the start: a node left to itself races that
    // step, and once enough nodes have voted the proposals commit without any view change.
    constexpr size_t precommitSize = 5;
    auto holdPrecommit = [&](bool _hold) {
        for (size_t i = 0; i < std::min(precommitSize, fakerMap.size()); i++)
        {
            std::dynamic_pointer_cast<FakePBFTConfig>(fakerMap[i]->pbftConfig())
                ->setHoldPrecommit(_hold);
        }
    };
    holdPrecommit(true);

    auto blockHeader = block->blockHeader();
    // the leader submit proposal
    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);
    // the future leader submit the proposal
    auto futureBlockHeader = futureBlock->blockHeader();
    futureLeader->pbftEngine()->asyncSubmitProposal(
        false, *futureBlock, futureBlockHeader->number(), futureBlockHeader->hash(), nullptr);

    auto cacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(leaderFaker->pbftEngine()->cacheProcessor());
    BOOST_CHECK(cacheProcessor->caches().size() == 1);
    auto cache = preparedCache(leaderFaker->pbftEngine()->cacheProcessor(), expectedProposal);
    BOOST_REQUIRE_MESSAGE(cache, "the leader holds no pre-prepared cache at " << expectedProposal);
    BOOST_CHECK(cache->index() == expectedProposal);

    auto futureCacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(futureLeader->pbftEngine()->cacheProcessor());
    auto futureCache =
        preparedCache(futureLeader->pbftEngine()->cacheProcessor(), futureBlockIndex);
    BOOST_CHECK(futureCacheProcessor->caches().size() == 1);
    BOOST_REQUIRE_MESSAGE(
        futureCache, "the future leader holds no pre-prepared cache at " << futureBlockIndex);
    BOOST_CHECK(futureCache->index() == futureBlockIndex);

    // Poll until the held nodes have the pre-prepares of both proposals. Counting caches, as
    // this loop used to, is not enough: a cache is created by the first message for its index,
    // so a prepare vote that overtakes the pre-prepare leaves a cache intoPrecommit() cannot
    // use (it dereferences the pre-prepare). The budget is generous because this loop drives
    // the fake nodes itself: on a loaded runner (macOS is the slow one here) 3s was not always
    // enough.
    auto precommitStartT = std::chrono::steady_clock::now();
    bool allPrePrepared = false;
    size_t deficientNode = 0;
    while (!allPrePrepared &&
           std::chrono::steady_clock::now() - precommitStartT < std::chrono::seconds(30))
    {
        for (auto const& otherNode : fakerMap)
        {
            otherNode.second->pbftEngine()->executeWorker();
        }
        allPrePrepared = true;
        for (size_t i = 0; i < std::min(precommitSize, fakerMap.size()); i++)
        {
            auto processor = fakerMap[i]->pbftEngine()->cacheProcessor();
            if (!preparedCache(processor, expectedProposal) ||
                !preparedCache(processor, futureBlockIndex))
            {
                allPrePrepared = false;
                deficientNode = i;
                break;
            }
        }
        if (!allPrePrepared)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Every later check depends on this, so a timeout has to stop the test here and say what
    // the deficient node actually holds.
    BOOST_REQUIRE_MESSAGE(allPrePrepared,
        "the first " << precommitSize << " nodes did not all cache the pre-prepares of "
                     << expectedProposal << " and " << futureBlockIndex << " within 30s; node "
                     << deficientNode << " is at block "
                     << fakerMap[deficientNode]->ledger()->blockNumber() << " and holds: "
                     << cachedIndexes(fakerMap[deficientNode]->pbftEngine()->cacheProcessor()));
    for (size_t i = 0; i < std::min(precommitSize, fakerMap.size()); i++)
    {
        auto processor = fakerMap[i]->pbftEngine()->cacheProcessor();
        for (auto index : {expectedProposal, futureBlockIndex})
        {
            auto cache = preparedCache(processor, index);
            BOOST_REQUIRE_MESSAGE(cache, "node " << i << " holds: " << cachedIndexes(processor));
            BOOST_CHECK(cache->index() == index);
            cache->intoPrecommit();
        }
    }
    // from here the nodes follow the protocol again: after the view change they have to
    // precommit the re-proposed blocks by themselves
    holdPrecommit(false);

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
            // Poll the io_context so that timer callbacks (onTimeout etc.)
            // and cross-thread notify() dispatches are processed.  On macOS
            // the io_context worker thread may not be scheduled promptly;
            // poll() gives the test thread an opportunity to drive pending
            // handlers without relying solely on the worker thread.
            faker->ioContext().poll();
            faker->pbftEngine()->executeWorkerByRoundbin();
        }
    }
    // check reach new view
    for (size_t i = 0; i < fakerMap.size(); i++)
    {
        auto faker = fakerMap[i];
        BOOST_CHECK_GT(faker->pbftConfig()->view(), 0);
        BOOST_CHECK_EQUAL(faker->pbftConfig()->toView(), (faker->pbftConfig()->view()));
        BOOST_CHECK_EQUAL(faker->pbftConfig()->timer()->changeCycle(), 0);
        BOOST_CHECK(!faker->pbftEngine()->isTimeout());
        BOOST_CHECK_EQUAL(faker->ledger()->blockNumber(), futureBlockIndex);
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos