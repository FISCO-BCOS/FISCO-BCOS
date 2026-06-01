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
 * @brief Regression test for FIB-172: PBFTCache::collectEnoughCommitReq() uses
 *        m_prePrepare->hash() for commit-quorum lookup.  CertiK flags this as
 *        Informational because intoPrecommit() derives m_precommit from m_prePrepare,
 *        so the hashes are guaranteed equal.  This test pins that invariant so a future
 *        refactor cannot silently break the assumption.
 * @file FIB172_CommitHashInvariantTest.cpp
 * @author: kyonRay
 * @date 2026-05-07
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
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

BOOST_FIXTURE_TEST_SUITE(FIB172_CommitHashInvariantTest, TestPromptFixture)

// ---------------------------------------------------------------------------
// Pin the invariant: after intoPrecommit(), m_precommit->hash() ==
// m_prePrepare->hash().  This is the implicit contract that lets
// collectEnoughCommitReq() safely key commit votes by m_prePrepare->hash().
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testIntoPrecommitPreservesHash)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto faker = fakerMap[0];

    // Build a minimal pre-prepare message.
    auto expectedIndex = faker->pbftConfig()->progressedIndex();
    auto expectedLeader = faker->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];

    auto ledgerConfig = leaderFaker->ledger()->ledgerConfig();
    auto parent = (leaderFaker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto block = leaderFaker->ledger()->init(parent->blockHeader(), true, expectedIndex, 0, 0);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    auto hash = hashImpl->hash(bytesConstRef(blockData->data(), blockData->size()));
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    auto fakedProposal =
        leaderMsgFixture->fakePBFTProposal(expectedIndex, hash, *blockData, {}, {});

    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        hash, expectedIndex, *blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    // Use FakePBFTCache which exposes intoPrecommit() and prePrepare().
    auto cache = std::make_shared<FakePBFTCache>(leaderFaker->pbftConfig(), expectedIndex);
    cache->addPrePrepareCache(pbftMsg);

    // Invariant pre-condition: precommit is null before intoPrecommit().
    BOOST_CHECK(cache->preCommitCache() == nullptr);

    // Trigger intoPrecommit().
    cache->intoPrecommit();

    // FIB-172 invariant: m_precommit is derived from m_prePrepare, so their hashes must match.
    BOOST_REQUIRE(cache->prePrepare() != nullptr);
    BOOST_REQUIRE(cache->preCommitCache() != nullptr);
    BOOST_CHECK_EQUAL(cache->prePrepare()->hash(), cache->preCommitCache()->hash());

    // Also check that the consensus-proposal hash (used in collectEnoughQuorum) is identical.
    BOOST_CHECK_EQUAL(cache->prePrepare()->consensusProposal()->hash(),
        cache->preCommitCache()->consensusProposal()->hash());
}

// ---------------------------------------------------------------------------
// Confirm collectEnoughCommitReq() returns false when no commit votes are
// present (smoke test that the function is callable and doesn't crash).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testCollectEnoughCommitReqNoVotes)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto faker = fakerMap[0];
    auto expectedIndex = faker->pbftConfig()->progressedIndex();
    auto expectedLeader = faker->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];

    auto ledgerConfig = leaderFaker->ledger()->ledgerConfig();
    auto parent = (leaderFaker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto block = leaderFaker->ledger()->init(parent->blockHeader(), true, expectedIndex, 0, 0);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    auto hash = hashImpl->hash(bytesConstRef(blockData->data(), blockData->size()));
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    auto fakedProposal =
        leaderMsgFixture->fakePBFTProposal(expectedIndex, hash, *blockData, {}, {});

    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        hash, expectedIndex, *blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    // Create a cache with 4 consensus nodes — quorum is 3.
    // With no commit votes the quorum check must return false.
    auto cache = std::make_shared<FakePBFTCache>(leaderFaker->pbftConfig(), expectedIndex);
    cache->addPrePrepareCache(pbftMsg);
    cache->intoPrecommit();

    // checkAndCommit() calls collectEnoughCommitReq() internally.
    // With zero votes committed it should return false.
    bool committed = cache->checkAndCommit();
    BOOST_CHECK_MESSAGE(
        !committed, "FIB-172: checkAndCommit() must return false with no commit votes");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
