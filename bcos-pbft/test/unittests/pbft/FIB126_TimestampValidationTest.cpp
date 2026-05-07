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
 * @brief Regression test for FIB-126: timestamp validation in PBFT proposal verification.
 *        A Byzantine leader must not be able to propose a block with a far-future timestamp.
 * @file FIB126_TimestampValidationTest.cpp
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
#include <bcos-utilities/Common.h>
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

BOOST_FIXTURE_TEST_SUITE(FIB126_TimestampValidationTest, TestPromptFixture)

// ---------------------------------------------------------------------------
// Helper: build an encoded block with a custom block header timestamp.
// ---------------------------------------------------------------------------
static bytes buildBlockWithTimestamp(CryptoSuite::Ptr cryptoSuite, PBFTFixture::Ptr fixture,
    BlockNumber blockIndex, int64_t blockTimestamp)
{
    auto ledgerConfig = fixture->ledger()->ledgerConfig();
    auto parent = (fixture->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    // init() respects the timestamp argument
    auto block =
        fixture->ledger()->init(parent->blockHeader(), true, blockIndex, 0, blockTimestamp);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    return *blockData;
}

// ---------------------------------------------------------------------------
// Case 1: Block timestamp far in the future — must be rejected.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testFutureTimestampRejected)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto expectedIndex = (fakerMap[0])->pbftConfig()->progressedIndex();
    auto expectedLeader = (fakerMap[0])->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];
    auto nonLeaderFaker = fakerMap[(expectedLeader + 1) % consensusNodeSize];

    // Build a block with a timestamp 1 hour in the future.
    int64_t farFuture = utcTime() + 3600LL * 1000;  // +1 hour in ms
    auto blockData = buildBlockWithTimestamp(cryptoSuite, leaderFaker, expectedIndex, farFuture);

    auto hash = hashImpl->hash(bytesConstRef(blockData.data(), blockData.size()));
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());

    auto fakedProposal = leaderMsgFixture->fakePBFTProposal(expectedIndex, hash, blockData, {}, {});
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        hash, expectedIndex, blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    // Bypass async tx verification (_needVerifyProposal=false) so the timestamp check
    // is the deciding factor.  _generatedFromNewView=true skips leader-identity check.
    auto result = nonLeaderFaker->pbftEngine()->handlePrePrepareMsg(pbftMsg, false, true, false);

    BOOST_CHECK_MESSAGE(!result, "FIB-126: PrePrepare with far-future timestamp must be rejected");
}

// ---------------------------------------------------------------------------
// Case 2: Block timestamp is not monotonically greater than last committed
//          block — must be rejected.
//
// The PBFTEngine::m_ledgerConfig is populated via getLedgerConfig() during
// fetchAndUpdateLedgerConfig() at init time.  The FakeLedger creates block
// headers with utcTime(), so m_ledgerConfig->timestamp() will be close to the
// current wall-clock time.  We therefore use a clearly-past timestamp (epoch=1)
// for the proposed block to guarantee proposedTs <= parentTs regardless of when
// the test runs.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testNonMonotonicTimestampRejected)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto expectedIndex = (fakerMap[0])->pbftConfig()->progressedIndex();
    auto expectedLeader = (fakerMap[0])->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];
    auto nonLeaderFaker = fakerMap[(expectedLeader + 1) % consensusNodeSize];

    // Inject a fake "committed block timestamp" of T = utcTime() so that any
    // proposal with timestamp <= T is rejected as non-monotonic.
    // (FakeLedger blocks are created with timestamp=0, so m_ledgerConfig->timestamp()
    //  defaults to 0 in the test engine; we override it here.)
    int64_t committedTs = utcTime();
    nonLeaderFaker->pbftEngine()->setCommittedBlockTimestampForTest(committedTs);

    // Proposed block has timestamp = committedTs - 1000 (strictly in the past).
    int64_t pastTimestamp = committedTs - 1000;
    auto blockData =
        buildBlockWithTimestamp(cryptoSuite, leaderFaker, expectedIndex, pastTimestamp);

    auto hash = hashImpl->hash(bytesConstRef(blockData.data(), blockData.size()));
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());

    auto fakedProposal = leaderMsgFixture->fakePBFTProposal(expectedIndex, hash, blockData, {}, {});
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        hash, expectedIndex, blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    auto result = nonLeaderFaker->pbftEngine()->handlePrePrepareMsg(pbftMsg, false, true, false);

    BOOST_CHECK_MESSAGE(
        !result, "FIB-126: PrePrepare with non-monotonic (past) timestamp must be rejected");
}

// ---------------------------------------------------------------------------
// Case 3: Block with valid timestamp (now) — must pass timestamp validation.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testValidTimestampAccepted)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto expectedIndex = (fakerMap[0])->pbftConfig()->progressedIndex();
    auto expectedLeader = (fakerMap[0])->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];
    auto nonLeaderFaker = fakerMap[(expectedLeader + 1) % consensusNodeSize];

    // Block timestamp is slightly in the future (parent + 1 second) — should pass.
    // We add 1000 ms to ensure proposedTs > parentTs even if both utcTime() calls
    // fall within the same millisecond.
    int64_t validTs = utcTime() + 1000;
    auto blockData = buildBlockWithTimestamp(cryptoSuite, leaderFaker, expectedIndex, validTs);

    auto hash = hashImpl->hash(bytesConstRef(blockData.data(), blockData.size()));
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());

    auto fakedProposal = leaderMsgFixture->fakePBFTProposal(expectedIndex, hash, blockData, {}, {});
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        hash, expectedIndex, blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    // ledgerConfig timestamp stays 0 (before genesis) — valid for now-timestamp
    auto result = nonLeaderFaker->pbftEngine()->handlePrePrepareMsg(pbftMsg, false, true, false);

    BOOST_CHECK_MESSAGE(result,
        "FIB-126: PrePrepare with valid (current) timestamp must not be rejected by timestamp "
        "check");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
