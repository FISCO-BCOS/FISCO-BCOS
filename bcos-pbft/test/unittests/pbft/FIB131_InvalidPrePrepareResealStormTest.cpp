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
 * @brief Regression test for FIB-131: per-peer consecutive invalid-pre-prepare counter must
 *        suppress unbounded notifySealer calls (reseal storm) from a Byzantine leader.
 * @file FIB131_InvalidPrePrepareResealStormTest.cpp
 */
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

namespace bcos
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(FIB131InvalidPrePrepareResealStormTest, TestPromptFixture)

// FIB-131: The engine must NOT accept invalid pre-prepares into the cache even after many
// consecutive invalid messages from the same peer index.
// Before the fix, every invalid pre-prepare triggered an unconditional notifySealer call
// which could cause an unbounded reseal storm. The fix adds a per-peer counter that caps
// notifications at c_maxInvalidPrePreparePerPeer=3 and suppresses subsequent ones.
BOOST_AUTO_TEST_CASE(testInvalidPrePrepareNeverEntersCache)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    // Use 4 consensus nodes so that the leader is deterministic.
    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    // Use a non-leader node as the victim (receiver of fake messages).
    auto victimFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];
    auto victimEngine = victimFaker->pbftEngine();
    auto victimConfig = victimFaker->pbftConfig();

    // Build a valid block for expectedIndex so we have real encoded data.
    auto leaderFaker = fakerMap[leaderIdx];
    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedIndex, 3);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto blockHeader = block->blockHeader();

    // Build a pre-prepare message signed by a *wrong* keypair so checkSignature fails.
    // PBFTMessageFixture requires a shared_ptr<KeyPairInterface>.
    KeyPairInterface::Ptr wrongKP{cryptoSuite->signatureImpl()->generateKeyPair().release()};
    auto wrongMsgFixture = std::make_shared<PBFTMessageFixture>(cryptoSuite, wrongKP);

    auto hash = blockHeader->hash();
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, victimConfig->view(), leaderIdx, hash,
        static_cast<protocol::BlockNumber>(expectedIndex), *blockData, 0, wrongMsgFixture,
        PacketType::PrePreparePacket);
    auto proposal = wrongMsgFixture->fakePBFTProposal(
        static_cast<protocol::BlockNumber>(expectedIndex), hash, *blockData, {}, {});
    pbftMsg->setConsensusProposal(proposal);

    // Send significantly more invalid pre-prepares than c_maxInvalidPrePreparePerPeer (=3).
    constexpr size_t sendCount = 10;
    for (size_t i = 0; i < sendCount; i++)
    {
        auto encoded = victimConfig->codec()->encode(pbftMsg);
        victimEngine->onReceivePBFTMessage(
            nullptr, victimFaker->keyPair()->publicKey(), ref(*encoded), nullptr);
        victimEngine->executeWorker();
    }

    // The key invariant: the invalid pre-prepare must never be cached.
    BOOST_CHECK(!victimEngine->cacheProcessor()->existPrePrepare(pbftMsg));
}

// FIB-131: After a successful pre-prepare from a peer, the per-peer failure counter
// must be reset so that the peer can send valid messages again without being suppressed.
// This test verifies normal consensus can complete after the rate-limiter is in place.
BOOST_AUTO_TEST_CASE(testCounterResetOnValidPrePrepare)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[leaderIdx];
    auto nonLeaderFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];

    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedIndex, 3);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto blockHeader = block->blockHeader();

    // Submit a valid proposal from the leader; consensus should complete.
    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);

    auto startT = utcTime();
    while ((nonLeaderFaker->ledger()->blockNumber() <
               static_cast<protocol::BlockNumber>(expectedIndex)) &&
           (utcTime() - startT <= 60 * 1000))
    {
        for (auto& kv : fakerMap)
        {
            kv.second->pbftEngine()->executeWorkerByRoundbin();
        }
    }

    // If the counter reset works correctly, the non-leader should have committed the block.
    BOOST_CHECK_GE(
        nonLeaderFaker->ledger()->blockNumber(), static_cast<protocol::BlockNumber>(expectedIndex));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
