/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file FIB142_ReceiverHashEquivocationTest.cpp
 * @brief Regression test for FIB-142 receiver-side: PBFTEngine::handlePrePrepareMsg
 *        must recompute the decoded block's hash and reject any (proposal
 *        hash, body) mismatch — otherwise a byzantine leader can equivocate
 *        by signing hash(A) over body(B) under the same proposal hash.
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

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB142_ReceiverHashEquivocation, TestPromptFixture)

BOOST_AUTO_TEST_CASE(receiver_rejects_decoded_hash_mismatch)
{
    // Boost log emits noise from the engine flow; keep it on for diagnosis.
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);

    constexpr size_t consensusNodeSize = 2;
    constexpr size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto expectedIndex = (fakerMap[0])->pbftConfig()->progressedIndex();
    auto expectedLeader = (fakerMap[0])->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];
    auto nonLeaderFaker = fakerMap[(expectedLeader + 1) % consensusNodeSize];

    // Build a real block (body B) from the leader's ledger.
    auto ledgerConfig = leaderFaker->ledger()->ledgerConfig();
    auto parent = (leaderFaker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto blockB = leaderFaker->ledger()->init(parent->blockHeader(), true, expectedIndex, 0, 0);
    auto blockBData = std::make_shared<bytes>();
    blockB->encode(*blockBData);

    // The byzantine leader signs over a totally unrelated hash (representing
    // the canonical hash of a different body A) but ships body B's encoding.
    auto bodyAHash = hashImpl->hash(bytesConstRef("equivocation-body-A"));
    BOOST_CHECK_NE(bodyAHash, blockB->blockHeader()->hash());

    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    auto fakedProposal = leaderMsgFixture->fakePBFTProposal(
        expectedIndex, bodyAHash, *blockBData, std::vector<int64_t>(), std::vector<bytes>());
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        bodyAHash, expectedIndex, bytes(), 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    auto data = leaderFaker->pbftConfig()->codec()->encode(pbftMsg);
    nonLeaderFaker->txpool()->setVerifyResult(true);
    nonLeaderFaker->pbftEngine()->onReceivePBFTMessage(
        nullptr, nonLeaderFaker->keyPair()->publicKey(), ref(*data), nullptr);
    nonLeaderFaker->pbftEngine()->executeWorker();

    // Give the engine a small window for any async path to settle. The
    // pre-prepare must NOT enter the cache because the receiver-side hash
    // recompute rejects the equivocation.
    auto startT = utcTime();
    while (utcTime() - startT < 500)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK(!nonLeaderFaker->pbftEngine()->cacheProcessor()->existPrePrepare(pbftMsg));

    for (auto& [_, node] : fakerMap)
    {
        node->stop();
    }
}

BOOST_AUTO_TEST_CASE(receiver_accepts_matched_hash)
{
    // Sanity check: an honestly-built proposal (hash(B) over body(B)) must
    // still be accepted into the cache.
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);

    constexpr size_t consensusNodeSize = 2;
    constexpr size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto expectedIndex = (fakerMap[0])->pbftConfig()->progressedIndex();
    auto expectedLeader = (fakerMap[0])->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[expectedLeader];
    auto nonLeaderFaker = fakerMap[(expectedLeader + 1) % consensusNodeSize];

    auto ledgerConfig = leaderFaker->ledger()->ledgerConfig();
    auto parent = (leaderFaker->ledger()->ledgerData())[ledgerConfig->blockNumber()];
    auto block = leaderFaker->ledger()->init(parent->blockHeader(), true, expectedIndex, 0, 0);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);

    auto realHash = block->blockHeader()->hash();
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    auto fakedProposal = leaderMsgFixture->fakePBFTProposal(
        expectedIndex, realHash, *blockData, std::vector<int64_t>(), std::vector<bytes>());
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), expectedLeader,
        realHash, expectedIndex, bytes(), 0, leaderMsgFixture, PacketType::PrePreparePacket);
    pbftMsg->setConsensusProposal(fakedProposal);

    auto data = leaderFaker->pbftConfig()->codec()->encode(pbftMsg);
    nonLeaderFaker->txpool()->setVerifyResult(true);
    nonLeaderFaker->pbftEngine()->onReceivePBFTMessage(
        nullptr, nonLeaderFaker->keyPair()->publicKey(), ref(*data), nullptr);
    nonLeaderFaker->pbftEngine()->executeWorker();

    auto startT = utcTime();
    while (!nonLeaderFaker->pbftEngine()->cacheProcessor()->existPrePrepare(pbftMsg) &&
           (utcTime() - startT <= 60 * 1000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK(nonLeaderFaker->pbftEngine()->cacheProcessor()->existPrePrepare(pbftMsg));

    for (auto& [_, node] : fakerMap)
    {
        node->stop();
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
