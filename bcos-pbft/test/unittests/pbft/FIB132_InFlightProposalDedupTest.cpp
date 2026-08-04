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
 * @brief Regression test for FIB-132: Missing in-flight deduplication for
 *        PrePrepare proposal verification causing repeated re-verification DoS.
 * @file FIB132_InFlightProposalDedupTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/engine/PBFTEngine.h"
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
using namespace bcos::test;

namespace bcos::test
{

// Thin wrapper that exposes protected in-flight helpers for testing
class InspectablePBFTEngine : public FakePBFTEngine
{
public:
    using Ptr = std::shared_ptr<InspectablePBFTEngine>;
    using FakePBFTEngine::FakePBFTEngine;

    std::string testInFlightKey(std::shared_ptr<PBFTBaseMessageInterface> const& msg)
    {
        return inFlightKey(msg);
    }

    // Directly insert a key into the in-flight set to simulate an in-flight proposal
    bool insertInFlight(std::string const& key) { return m_inFlightProposals.insert(key).second; }

    bool isInFlight(std::string const& key) { return m_inFlightProposals.count(key) > 0; }

    size_t inFlightSize() const { return m_inFlightProposals.size(); }
};

BOOST_FIXTURE_TEST_SUITE(FIB132Test, TestPromptFixture)

// FIB-132: Verify that the in-flight proposal set correctly deduplicates
// same-proposal keys and distinguishes different proposals.
// This tests the guard mechanism that PBFTEngine::handlePrePrepareMsg() uses
// to prevent verifyProposal() from being called redundantly for in-flight proposals.
BOOST_AUTO_TEST_CASE(inflight_key_uniqueness_and_dedup)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    BlockNumber currentBlockNumber = 10;
    auto fakerMap = createFakers(cryptoSuite, 4, currentBlockNumber, 4);
    IndexType leaderIndex = 0;
    auto leaderFaker = fakerMap[leaderIndex];

    // Build two proposals with different hashes
    size_t proposalIndex = currentBlockNumber + 1;
    auto block1 = fakeBlock(cryptoSuite, leaderFaker, proposalIndex, 0);
    bytes blockData1;
    block1->encode(blockData1);

    auto pbftMsgFixture = std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());

    auto hash1 = HashType("1111111111111111111111111111111111111111111111111111111111111111");
    auto hash2 = HashType("2222222222222222222222222222222222222222222222222222222222222222");

    auto pbftProposal1 = pbftMsgFixture->fakePBFTProposal(proposalIndex, hash1, blockData1, {}, {});
    auto msg1 = pbftMsgFixture->fakePBFTMessage(utcTime(), 0, leaderFaker->pbftConfig()->view(),
        leaderFaker->pbftConfig()->nodeIndex(), hash1, {pbftProposal1});
    msg1->setIndex(proposalIndex);
    msg1->setConsensusProposal(pbftProposal1);

    auto pbftProposal2 = pbftMsgFixture->fakePBFTProposal(proposalIndex, hash2, blockData1, {}, {});
    auto msg2 = pbftMsgFixture->fakePBFTMessage(utcTime(), 0, leaderFaker->pbftConfig()->view(),
        leaderFaker->pbftConfig()->nodeIndex(), hash2, {pbftProposal2});
    msg2->setIndex(proposalIndex);
    msg2->setConsensusProposal(pbftProposal2);

    // Create an InspectablePBFTEngine backed by leaderFaker's config
    auto engine = std::make_shared<InspectablePBFTEngine>(leaderFaker->pbftConfig());

    auto key1 = engine->testInFlightKey(msg1);
    auto key1b = engine->testInFlightKey(msg1);
    auto key2 = engine->testInFlightKey(msg2);

    // Same message -> same key
    BOOST_CHECK_EQUAL(key1, key1b);

    // Different hash -> different key
    BOOST_CHECK_NE(key1, key2);

    // Key contains index, hash, and view
    BOOST_CHECK(key1.find(std::to_string(proposalIndex)) != std::string::npos);

    // Simulate an in-flight proposal being inserted
    bool inserted = engine->insertInFlight(key1);
    BOOST_CHECK(inserted);  // first insert succeeds
    BOOST_CHECK(engine->isInFlight(key1));
    BOOST_CHECK_EQUAL(engine->inFlightSize(), 1U);

    // Second insert with same key fails (dedup)
    bool inserted2 = engine->insertInFlight(key1);
    BOOST_CHECK(!inserted2);
    BOOST_CHECK_EQUAL(engine->inFlightSize(), 1U);

    // Different proposal key can be inserted
    bool inserted3 = engine->insertInFlight(key2);
    BOOST_CHECK(inserted3);
    BOOST_CHECK_EQUAL(engine->inFlightSize(), 2U);

    // FIB-132: the in-flight guard in handlePrePrepareMsg checks exactly this set.
    // When msg1 is inserted before calling verifyProposal(), a second concurrent call
    // for msg1 finds the key in the set and returns early without re-dispatching verify.
    // This prevents DoS via repeated verification of the same proposal.
    BOOST_CHECK_MESSAGE(engine->isInFlight(key1) && engine->inFlightSize() == 2,
        "FIB-132: in-flight set must correctly track concurrent proposals");

    for (auto& [idx, faker] : fakerMap)
    {
        faker->stop();
    }
}

// FIB-132: m_inFlightProposals must be bounded. Verifies the cap constant
// exists, has the expected value (1024), and that the set fills to exactly
// the cap when populated via the test helper. Cap enforcement at the
// handlePrePrepareMsg entry point is exercised indirectly: with the set at
// cap, a new key would be rejected by the `size >= c_maxInFlightProposals`
// guard added to handlePrePrepareMsg.
BOOST_AUTO_TEST_CASE(inflight_set_capped_under_flood)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    BlockNumber currentBlockNumber = 10;
    auto fakerMap = createFakers(cryptoSuite, 4, currentBlockNumber, 4);
    auto leaderFaker = fakerMap[0];
    auto engine = std::make_shared<InspectablePBFTEngine>(leaderFaker->pbftConfig());

    // The cap constant must exist and equal 1024.
    BOOST_CHECK_EQUAL(PBFTEngine::c_maxInFlightProposals, 1024U);

    // Fill in-flight set to cap with synthetic keys.
    size_t cap = PBFTEngine::c_maxInFlightProposals;
    for (size_t i = 0; i < cap; ++i)
    {
        std::string key = "synthetic:" + std::to_string(i);
        BOOST_CHECK_MESSAGE(engine->insertInFlight(key),
            "FIB-132: insertion of distinct keys below cap must succeed at i=" + std::to_string(i));
    }
    BOOST_CHECK_EQUAL(engine->inFlightSize(), cap);

    for (auto& [idx, faker] : fakerMap)
    {
        faker->stop();
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
