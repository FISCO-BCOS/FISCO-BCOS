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
 * @brief Regression test for FIB-130: handlePrePrepareMsg must cross-check the outer PBFT
 *        message index against the actual block-header number decoded from the proposal data.
 * @file FIB130_PrePrepareMetadataCrosscheckTest.cpp
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

namespace bcos
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(FIB130PrePrepareMetadataCrosscheckTest, TestPromptFixture)

// FIB-130: A pre-prepare whose outer PBFT message index does not match the block-header
// number embedded in the proposal data must be rejected by the receiver.
BOOST_AUTO_TEST_CASE(testRejectPrePrepareWithMismatchedBlockNumber)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    IndexType expectedIndex = fakerMap[0]->pbftConfig()->progressedIndex();  // = 11
    IndexType leaderIdx = fakerMap[0]->pbftConfig()->leaderIndex(expectedIndex);
    auto leaderFaker = fakerMap[leaderIdx];
    auto nonLeaderFaker = fakerMap[(leaderIdx + 1) % consensusNodeSize];

    // Build a real block for index `expectedIndex` and encode it.
    auto block = fakeBlock(cryptoSuite, leaderFaker, expectedIndex, 3);
    auto blockData = std::make_shared<bytes>();
    block->encode(*blockData);
    auto blockHeader = block->blockHeader();

    // Build a fake PBFTMessage that claims to be for a *different* index than the block.
    // This mimics an attacker who swaps the outer index while keeping the block data intact.
    auto leaderMsgFixture =
        std::make_shared<PBFTMessageFixture>(cryptoSuite, leaderFaker->keyPair());
    BlockNumber spoofedIndex = expectedIndex + 5;  // deliberately wrong
    auto hash = blockHeader->hash();
    auto pbftMsg = fakePBFTMessage(utcTime(), 1, leaderFaker->pbftConfig()->view(), leaderIdx, hash,
        spoofedIndex, *blockData, 0, leaderMsgFixture, PacketType::PrePreparePacket);

    // The consensus proposal carries the REAL block data but the outer PBFT index is wrong.
    auto proposal = leaderMsgFixture->fakePBFTProposal(spoofedIndex, hash, *blockData, {}, {});
    pbftMsg->setConsensusProposal(proposal);

    auto encodedData = leaderFaker->pbftConfig()->codec()->encode(pbftMsg);
    nonLeaderFaker->pbftEngine()->onReceivePBFTMessage(
        nullptr, nonLeaderFaker->keyPair()->publicKey(), ref(*encodedData), nullptr);
    nonLeaderFaker->pbftEngine()->executeWorker();

    // The cross-check at handlePrePrepareMsg should reject the message before it enters the cache.
    BOOST_CHECK(!nonLeaderFaker->pbftEngine()->cacheProcessor()->existPrePrepare(pbftMsg));
}

// FIB-130: A pre-prepare whose outer PBFT index matches the block-header number must pass
// the cross-check and be accepted normally (happy path).
BOOST_AUTO_TEST_CASE(testAcceptPrePrepareWithMatchingBlockNumber)
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

    // Correct outer index = block-header number → should pass cross-check.
    leaderFaker->pbftEngine()->asyncSubmitProposal(
        false, *block, blockHeader->number(), blockHeader->hash(), nullptr);

    auto startT = utcTime();
    while (utcTime() - startT <= 60 * 1000)
    {
        for (auto& kv : fakerMap)
        {
            kv.second->pbftEngine()->executeWorkerByRoundbin();
        }
        if (nonLeaderFaker->ledger()->blockNumber() >=
            static_cast<protocol::BlockNumber>(expectedIndex))
        {
            break;
        }
    }
    // Verify the block was committed (consensus completed successfully).
    BOOST_CHECK_GE(
        nonLeaderFaker->ledger()->blockNumber(), static_cast<protocol::BlockNumber>(expectedIndex));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
