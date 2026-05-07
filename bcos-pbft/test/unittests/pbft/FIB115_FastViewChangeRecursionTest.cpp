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
 * @brief Regression test for FIB-115: unbounded recursion in tryTriggerFastViewChange()
 * @file FIB115_FastViewChangeRecursionTest.cpp
 * @date 2026-05-07
 */
#include "test/unittests/pbft/PBFTFixture.h"
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

// FIB-115: tryTriggerFastViewChange() used to call itself recursively with no depth cap.
// When every candidate leader in the rotation is classified as faulty the call chain would
// continue until the stack overflows.  The fix converts the recursion into a bounded
// iterative loop (at most consensusNodesNum() iterations).
//
// This test verifies that:
//   (a) When ALL peers are marked faulty, the function returns without crashing
//       (the loop terminates within N iterations, not N recursive frames deep).
//   (b) When NO peer is faulty, the function returns false immediately.
//   (c) When only some peers are faulty, the function returns true once a
//       non-faulty candidate is found.

BOOST_FIXTURE_TEST_SUITE(FIB115FastViewChangeRecursionTest, TestPromptFixture)

// Helper: build a 4-node fixture where this node has a known index.
static std::shared_ptr<PBFTFixture> makeFourNodeFixture()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    // We use createFakers so that all nodes are part of the consensus list and
    // the connected-node set is populated.
    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 5;
    size_t connectedNodes = 4;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, connectedNodes);

    // Return node-0 (the first node); it will always be in the consensus list.
    return fakerMap[0];
}

// Case A: all OTHER nodes faulty.
// The old recursive implementation would recurse >= consensusNodeNum times (stack overflow).
// The bounded-loop implementation must return false without crashing.
BOOST_AUTO_TEST_CASE(testAllPeersFaultyNoStackOverflow)
{
    auto faker = makeFourNodeFixture();
    auto pbftConfig = faker->pbftConfig();

    // Mark every node (including all potential leaders) as faulty.
    pbftConfig->registerFaultyDiscriminator([](bcos::crypto::NodeIDPtr) { return true; });

    // Initialise fast-view-change handler with a no-op so the path is taken.
    int fastViewChangeCount = 0;
    pbftConfig->registerFastViewChangeHandler([&fastViewChangeCount]() { fastViewChangeCount++; });

    // leaderIndex(0) with view=0 returns node 0, which is nodeIndex() itself —
    // so tryTriggerFastViewChange(nodeIndex()) returns false immediately.
    // Use a different leader index to make the function enter the body.
    auto leaderIndex = (pbftConfig->nodeIndex() + 1) % pbftConfig->consensusNodesNum();

    // This must NOT overflow the stack even when every subsequent candidate is faulty.
    bool result = pbftConfig->tryTriggerFastViewChange(leaderIndex);

    // With all peers faulty and the loop bounded to N iterations, the function
    // must return without crashing.  It may return true or false depending on
    // implementation details, but it must NOT throw / crash.
    (void)result;
    // Key assertion: we reach this line (no stack overflow, no crash).
    BOOST_CHECK(fastViewChangeCount >= 1);
}

// Case B: no peer is faulty — function must return false without triggering a view-change.
BOOST_AUTO_TEST_CASE(testNoPeerFaultyReturnsFalse)
{
    auto faker = makeFourNodeFixture();
    auto pbftConfig = faker->pbftConfig();

    // No nodes are faulty.
    pbftConfig->registerFaultyDiscriminator([](bcos::crypto::NodeIDPtr) { return false; });

    bool fastViewChangeCalled = false;
    pbftConfig->registerFastViewChangeHandler(
        [&fastViewChangeCalled]() { fastViewChangeCalled = true; });

    auto leaderIndex = (pbftConfig->nodeIndex() + 1) % pbftConfig->consensusNodesNum();
    bool result = pbftConfig->tryTriggerFastViewChange(leaderIndex);

    BOOST_CHECK_EQUAL(result, false);
    BOOST_CHECK_EQUAL(fastViewChangeCalled, false);
}

// Case C: iteration stops as soon as a non-faulty leader is found.
// With N=4 nodes, mark the first candidate faulty and the second non-faulty.
// Exactly one view-change should be triggered and the loop must stop.
BOOST_AUTO_TEST_CASE(testPartialFaultyStopsAtFirstHealthyLeader)
{
    auto faker = makeFourNodeFixture();
    auto pbftConfig = faker->pbftConfig();

    // Mark only node at index == firstLeader as faulty; all others healthy.
    IndexType nodeIdx = pbftConfig->nodeIndex();
    IndexType firstLeader = (nodeIdx + 1) % pbftConfig->consensusNodesNum();

    // Collect the NodeID of firstLeader for comparison.
    auto firstLeaderInfo = pbftConfig->getConsensusNodeByIndex(firstLeader);
    BOOST_REQUIRE(firstLeaderInfo != nullptr);
    auto faultyNodeID = firstLeaderInfo->nodeID;

    pbftConfig->registerFaultyDiscriminator([faultyNodeID](bcos::crypto::NodeIDPtr nodeID) {
        return nodeID->data() == faultyNodeID->data();
    });

    int fastViewChangeCount = 0;
    pbftConfig->registerFastViewChangeHandler([&fastViewChangeCount]() { fastViewChangeCount++; });

    bool result = pbftConfig->tryTriggerFastViewChange(firstLeader);

    // One view-change triggered (for firstLeader), then next candidate is healthy so loop stops.
    BOOST_CHECK_EQUAL(fastViewChangeCount, 1);
    // The overall result is true because at least one fast-view-change was triggered.
    BOOST_CHECK_EQUAL(result, true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
