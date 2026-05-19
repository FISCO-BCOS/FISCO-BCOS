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
 * @brief Regression test for FIB-128: tryToApplyCommitQueue() must reject a committed
 *        proposal whose block number is not consecutive with the last applied proposal.
 * @file FIB128_CommitParentHashCheckTest.cpp
 * @date 2026-05-07
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
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

// FIB-128: tryToApplyCommitQueue() had a TODO comment noting that parent block header
// fields (number, hash, timestamp) should be checked before executing a committed proposal,
// but the check was never implemented.  A proposal with a non-consecutive block number
// relative to the last applied proposal would reach applyStateMachine() — which then hits
// a FATAL log — instead of being rejected at the admission stage.
//
// The fix adds two early-reject guards before applyStateMachine():
//   (1) proposal->index() == lastAppliedProposal->index() + 1, and
//   (2) proposalHeader.timestamp >= parentHeader.timestamp (when both encoded headers are
//       available locally).
//
// Test cases:
//   A. Consecutive proposal (index == last+1, timestamp monotonic, empty data): accepted.
//   B. Non-monotonic timestamp: rejected by clause (2).
//   C. Stale lastApplied with non-consecutive index: rejected by clause (1).

BOOST_FIXTURE_TEST_SUITE(FIB128CommitParentHashCheckTest, TestPromptFixture)

static bcos::crypto::Hash::Ptr g_hashImpl;

// Build a single-consensus-node cluster initialised to block 10.
static std::tuple<FakeCacheProcessor::Ptr, PBFTConfig::Ptr, PBFTFixture::Ptr> makeFixture()
{
    g_hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(g_hashImpl, signImpl, nullptr);

    size_t consensusNodeSize = 1;
    size_t connectedNodes = 1;
    BlockNumber currentBlock = 10;
    auto fakerMap = createFakers(cryptoSuite, consensusNodeSize, currentBlock, connectedNodes);
    auto faker = fakerMap[0];
    faker->init();

    auto pbftConfig = faker->pbftConfig();
    auto cacheProcessor =
        std::dynamic_pointer_cast<FakeCacheProcessor>(faker->pbftEngine()->cacheProcessor());
    BOOST_REQUIRE(cacheProcessor != nullptr);
    return {cacheProcessor, pbftConfig, faker};
}

// Case A: consecutive proposal is accepted — the normal path must still work after the fix.
BOOST_AUTO_TEST_CASE(testConsecutiveProposalAccepted)
{
    auto [cacheProcessor, pbftConfig, faker] = makeFixture();

    // After init: committedProposal->index() == 10, expectedCheckPoint == 11.
    BlockNumber committedIdx = pbftConfig->committedProposal()->index();
    BOOST_REQUIRE(committedIdx == 10);
    BlockNumber expectedCP = pbftConfig->expectedCheckPoint();
    BOOST_REQUIRE(expectedCP == committedIdx + 1);  // == 11

    // Build a consecutive proposal at expectedCheckPoint (== 11).
    // getAppliedCheckPointProposal(10) == committedProposal (index 10).
    // FIB-128 check: proposal->index() (11) == lastApplied->index() (10) + 1  → PASSES.
    auto pbftMsgFactory = pbftConfig->pbftMessageFactory();
    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(expectedCP);
    proposal->setHash(g_hashImpl->hash(std::string("block11")));
    proposal->setData(bcos::bytes{});  // FakeScheduler accepts empty body

    cacheProcessor->pushToCommittedQueueForTest(proposal);

    // The admission check passes → tryToApplyCommitQueue returns true.
    bool result = cacheProcessor->tryToApplyCommitQueue();
    BOOST_CHECK_EQUAL(result, true);
}

// Case C: proposal header timestamp < parent timestamp.  StateMachine::apply() does not
// rewrite the proposal's timestamp before execution, so a byzantine leader can otherwise
// inject a non-monotonic timestamp into the chain.  The FIB-128 timestamp clause must reject
// the proposal at the admission stage.
BOOST_AUTO_TEST_CASE(testProposalWithNonMonotonicTimestampRejected)
{
    auto [cacheProcessor, pbftConfig, faker] = makeFixture();
    BlockNumber committedIdx = pbftConfig->committedProposal()->index();
    BlockNumber expectedCP = pbftConfig->expectedCheckPoint();

    // Build a "parent" block at index=10 with timestamp = 1000.  Encode just the header —
    // this matches how StateMachine produces _executedProposal->data() in production.
    int64_t parentTimestamp = 1000;
    auto parentBlock = faker->ledger()->init(nullptr, true, committedIdx, 0, parentTimestamp);
    bytes parentHeaderBuf;
    parentBlock->blockHeader()->encode(parentHeaderBuf);

    auto pbftMsgFactory = pbftConfig->pbftMessageFactory();
    auto knownParent = pbftMsgFactory->createPBFTProposal();
    knownParent->setIndex(committedIdx);
    knownParent->setHash(parentBlock->blockHeader()->hash());
    knownParent->setData(parentHeaderBuf);
    cacheProcessor->injectStaleLastAppliedForTest(knownParent);

    // Proposal at expectedCP with timestamp = parentTimestamp - 500 (non-monotonic).
    int64_t proposalTimestamp = parentTimestamp - 500;
    auto proposalBlock =
        faker->ledger()->init(parentBlock->blockHeader(), true, expectedCP, 0, proposalTimestamp);
    bytes proposalData;
    proposalBlock->encode(proposalData);

    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(expectedCP);
    proposal->setHash(g_hashImpl->hash(proposalData));
    proposal->setData(proposalData);
    cacheProcessor->pushToCommittedQueueForTest(proposal);

    // proposalHeader.timestamp (500) < parentHeader.timestamp (1000) → reject.
    BOOST_CHECK_EQUAL(cacheProcessor->tryToApplyCommitQueue(), false);
}

// Case B: injected stale lastApplied causes proposal->index() != lastApplied->index()+1.
//
// Simulates a corrupt or stale cache state where the "last applied proposal" has an index
// that is two blocks behind the committed proposal, creating a gap.  The FIB-128 guard
// must catch this and return false rather than calling applyStateMachine() with a gap.
//
// Without FIB-128 fix: applyStateMachine() is called with a mismatched parent, and
//   StateMachine::apply() hits a FATAL assertion ("invalid lastAppliedProposal").
// With    FIB-128 fix: early return false at the new admission guard.
BOOST_AUTO_TEST_CASE(testNonConsecutiveParentIndexRejected)
{
    auto [cacheProcessor, pbftConfig, faker] = makeFixture();

    BlockNumber committedIdx = pbftConfig->committedProposal()->index();
    BOOST_REQUIRE(committedIdx == 10);
    BlockNumber expectedCP = pbftConfig->expectedCheckPoint();
    BOOST_REQUIRE(expectedCP == 11);

    // Inject a stale lastApplied whose index is committedIdx-1 (== 9).
    // This simulates a corrupt cache state where getAppliedCheckPointProposal(10) returns
    // a proposal at index 9 instead of 10.
    auto pbftMsgFactory = pbftConfig->pbftMessageFactory();
    auto staleParent = pbftMsgFactory->createPBFTProposal();
    staleParent->setIndex(committedIdx - 1);  // == 9: two behind expectedCP-1=10
    staleParent->setHash(g_hashImpl->hash(std::string("stale")));
    cacheProcessor->injectStaleLastAppliedForTest(staleParent);

    // Build the committed proposal at expectedCheckPoint (== 11).
    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(expectedCP);  // == 11
    proposal->setHash(g_hashImpl->hash(std::string("block11")));
    proposal->setData(bcos::bytes{});
    cacheProcessor->pushToCommittedQueueForTest(proposal);

    // With FIB-128 fix: proposal->index() (11) != staleParent->index() (9) + 1 (10)
    //   → rejected (return false).
    // Without the fix: applyStateMachine is called → StateMachine FATAL assertion.
    bool result = cacheProcessor->tryToApplyCommitQueue();
    BOOST_CHECK_EQUAL(result, false);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
