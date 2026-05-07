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
 * @brief Regression test for FIB-133: handlePrePrepareMsg() must reject PrePrepare
 *        messages whose view() > local view when not generated from a new-view round.
 * @file FIB133_HigherViewPrePrepareCacheTest.cpp
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

// FIB-133: In the normal (!_generatedFromNewView) PrePrepare path, handlePrePrepareMsg()
// computes the expected leader with leaderIndex(_prePrepareMsg->index()), which derives
// the leader from the LOCAL m_view rather than from _prePrepareMsg->view().
// At the same time checkPBFTMsgState() accepts messages with view() >= local view (up to a
// watermark limit), so a byzantine node can send a PrePrepare with a higher view that still
// passes the leader check (since the leader is computed from local view) and then the node
// calls broadcastPrepareMsg() using m_config->view() — creating a cross-view inconsistency.
//
// Fix: in the !_generatedFromNewView path, reject PrePrepare messages whose view()
// differs from the local view (i.e. require _prePrepareMsg->view() == m_config->view()).
//
// Test cases:
//   A. Same-view PrePrepare from the correct leader: accepted (normal path unaffected).
//   B. Higher-view PrePrepare (_prePrepareMsg->view() > m_config->view()): rejected.
//   C. Lower-view PrePrepare (_prePrepareMsg->view() < m_config->view()): already rejected
//      by checkPBFTMsgState() — verify this still holds.

BOOST_FIXTURE_TEST_SUITE(FIB133HigherViewPrePrepareCacheTest, TestPromptFixture)

// Build a 4-node cluster and return the leader's fixture for a given view.
static std::map<IndexType, PBFTFixture::Ptr> makeCluster()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t connectedNodes = 4;
    BlockNumber currentBlock = 5;
    return createFakers(cryptoSuite, consensusNodeSize, currentBlock, connectedNodes);
}

// Build a minimal block body so the proposal data is non-empty.
static bytes fakeBlockData(PBFTFixture::Ptr faker, BlockNumber proposalIndex)
{
    auto ledgerConfig = faker->ledger()->ledgerConfig();
    auto parent = faker->ledger()->ledgerData()[ledgerConfig->blockNumber()];
    auto block = faker->ledger()->init(parent->blockHeader(), true, proposalIndex, 0, 0);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    return *blockData;
}

// Case A: same-view PrePrepare from the correct leader is accepted.
BOOST_AUTO_TEST_CASE(testSameViewPrePrepareAccepted)
{
    auto fakerMap = makeCluster();

    // Find the leader for view=0, proposalIndex=committedIndex+1.
    // leader = (proposalIndex/period + view) % N = (6/1 + 0) % 4 = 2
    PBFTFixture::Ptr leaderFaker = nullptr;
    PBFTFixture::Ptr followerFaker = nullptr;
    for (auto& [idx, faker] : fakerMap)
    {
        auto leader = faker->pbftConfig()->leaderIndex(faker->pbftConfig()->expectedCheckPoint());
        if (idx == leader)
        {
            leaderFaker = faker;
        }
        else if (followerFaker == nullptr)
        {
            followerFaker = faker;
        }
    }
    BOOST_REQUIRE(leaderFaker != nullptr);
    BOOST_REQUIRE(followerFaker != nullptr);

    auto followerConfig = followerFaker->pbftConfig();
    auto followerEngine = followerFaker->pbftEngine();

    ViewType localView = followerConfig->view();
    BlockNumber proposalIndex = followerConfig->expectedCheckPoint();
    IndexType leaderIdx = followerConfig->leaderIndex(proposalIndex);

    // Build a same-view PrePrepare from the correct leader.
    auto pbftMsgFactory = followerConfig->pbftMessageFactory();
    auto prePrepare = pbftMsgFactory->createPBFTMsg();
    prePrepare->setIndex(proposalIndex);
    prePrepare->setView(localView);  // same view
    prePrepare->setGeneratedFrom(leaderIdx);
    prePrepare->setPacketType(PacketType::PrePreparePacket);
    prePrepare->setVersion(followerConfig->pbftMsgDefaultVersion());
    prePrepare->setTimestamp(utcTime());

    auto blockData = fakeBlockData(leaderFaker, proposalIndex);
    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(proposalIndex);
    proposal->setHash(followerConfig->cryptoSuite()->hashImpl()->hash(blockData));
    proposal->setData(blockData);
    prePrepare->setConsensusProposal(proposal);
    prePrepare->setHash(proposal->hash());

    // handlePrePrepareMsg with _needVerifyProposal=false, _needCheckSignature=false
    bool result = followerEngine->handlePrePrepareMsg(prePrepare, false, false, false);
    // Same-view message from correct leader must be accepted.
    BOOST_CHECK_EQUAL(result, true);
}

// Case B: higher-view PrePrepare (view > local view) must be rejected in the normal path.
// This is the core FIB-133 scenario: a byzantine leader sends a PrePrepare with view=1
// while the cluster is still at view=0.  Without the fix, the leader check uses local
// view=0 and still passes (since the node IS the leader for view=0).  With the fix, the
// view mismatch is detected first and the message is rejected.
BOOST_AUTO_TEST_CASE(testHigherViewPrePrepareRejected)
{
    auto fakerMap = makeCluster();

    PBFTFixture::Ptr followerFaker = fakerMap[0];
    auto followerConfig = followerFaker->pbftConfig();
    auto followerEngine = followerFaker->pbftEngine();

    ViewType localView = followerConfig->view();
    ViewType higherView = localView + 1;
    BlockNumber proposalIndex = followerConfig->expectedCheckPoint();

    // The expected leader at localView for proposalIndex.
    IndexType leaderIdx = followerConfig->leaderIndex(proposalIndex);

    auto pbftMsgFactory = followerConfig->pbftMessageFactory();
    auto prePrepare = pbftMsgFactory->createPBFTMsg();
    prePrepare->setIndex(proposalIndex);
    prePrepare->setView(higherView);  // HIGHER view — the FIB-133 case
    prePrepare->setGeneratedFrom(leaderIdx);
    prePrepare->setPacketType(PacketType::PrePreparePacket);
    prePrepare->setVersion(followerConfig->pbftMsgDefaultVersion());
    prePrepare->setTimestamp(utcTime());

    // Use the leader's faker to build valid block data.
    PBFTFixture::Ptr leaderFaker = fakerMap.count(leaderIdx) ? fakerMap[leaderIdx] : fakerMap[0];
    auto blockData = fakeBlockData(leaderFaker, proposalIndex);
    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(proposalIndex);
    proposal->setHash(followerConfig->cryptoSuite()->hashImpl()->hash(blockData));
    proposal->setData(blockData);
    prePrepare->setConsensusProposal(proposal);
    prePrepare->setHash(proposal->hash());

    // With FIB-133 fix: view mismatch (higherView != localView) → rejected (false).
    // Without fix: leader check uses localView, may accept this higher-view message.
    bool result = followerEngine->handlePrePrepareMsg(prePrepare, false, false, false);
    BOOST_CHECK_EQUAL(result, false);
}

// Case C: lower-view PrePrepare is rejected by existing checkPBFTMsgState() — unchanged.
BOOST_AUTO_TEST_CASE(testLowerViewPrePrepareAlreadyRejected)
{
    auto fakerMap = makeCluster();

    PBFTFixture::Ptr followerFaker = fakerMap[0];
    auto followerConfig = followerFaker->pbftConfig();
    auto followerEngine = followerFaker->pbftEngine();

    ViewType localView = followerConfig->view();
    BlockNumber proposalIndex = followerConfig->expectedCheckPoint();
    IndexType leaderIdx = followerConfig->leaderIndex(proposalIndex);

    // Advance local view to 2 so we can send a lower-view message.
    followerConfig->setView(2);
    followerConfig->setToView(2);

    auto pbftMsgFactory = followerConfig->pbftMessageFactory();
    auto prePrepare = pbftMsgFactory->createPBFTMsg();
    prePrepare->setIndex(proposalIndex);
    prePrepare->setView(localView);  // view=0 < current view=2
    prePrepare->setGeneratedFrom(leaderIdx);
    prePrepare->setPacketType(PacketType::PrePreparePacket);
    prePrepare->setVersion(followerConfig->pbftMsgDefaultVersion());
    prePrepare->setTimestamp(utcTime());

    PBFTFixture::Ptr leaderFaker = fakerMap.count(leaderIdx) ? fakerMap[leaderIdx] : fakerMap[0];
    auto blockData = fakeBlockData(leaderFaker, proposalIndex);
    auto proposal = pbftMsgFactory->createPBFTProposal();
    proposal->setIndex(proposalIndex);
    proposal->setHash(followerConfig->cryptoSuite()->hashImpl()->hash(blockData));
    proposal->setData(blockData);
    prePrepare->setConsensusProposal(proposal);
    prePrepare->setHash(proposal->hash());

    // Lower-view is already rejected by checkPBFTMsgState() — must stay false.
    bool result = followerEngine->handlePrePrepareMsg(prePrepare, false, false, false);
    BOOST_CHECK_EQUAL(result, false);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
