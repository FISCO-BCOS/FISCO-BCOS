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
 * @brief Regression test for FIB-127: onRecvCommittedProposalsResponse must verify signature
 *        proofs on each recovered proposal before loading into cache.
 * @file FIB127_LogRecoverySigCheckTest.cpp
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-pbft/pbft/engine/PBFTLogSync.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTMessage.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTProposal.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{

// Thin subclass that exposes the protected response handler for unit testing.
class TestPBFTLogSync : public PBFTLogSync
{
public:
    using Ptr = std::shared_ptr<TestPBFTLogSync>;
    TestPBFTLogSync(PBFTConfig::Ptr _config, PBFTCacheProcessor::Ptr _cache)
      : PBFTLogSync(_config, _cache)
    {}
    void testOnRecvCommittedProposalsResponse(bcos::Error::Ptr _error,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        bcos::protocol::BlockNumber _startIndex, size_t _offset)
    {
        onRecvCommittedProposalsResponse(_error, _nodeID, _data, _startIndex, _offset, nullptr);
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB127LogRecoverySigCheckTest, TestPromptFixture)

// FIB-127: Proposals received via CommittedProposalResponse without any signature proof must
// be silently discarded rather than loaded into the proposal cache.
BOOST_AUTO_TEST_CASE(testDropProposalWithNoSigProofs)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto receiverFaker = fakerMap[0];
    auto config = receiverFaker->pbftConfig();
    auto cacheProcessor = std::dynamic_pointer_cast<FakeCacheProcessor>(
        receiverFaker->pbftEngine()->cacheProcessor());

    // Build a fake CommittedProposalResponse containing one proposal with zero sig proofs.
    auto proposal = std::make_shared<PBFTProposal>();
    proposal->setIndex(static_cast<protocol::BlockNumber>(currentBlockNumber + 1));
    auto blockHash = hashImpl->hash(bytesConstRef("fib127-test-block"));
    proposal->setHash(blockHash);
    // Intentionally add NO signature proofs to simulate an attacker-injected proposal.

    auto responseMsg = std::make_shared<PBFTMessage>();
    responseMsg->setPacketType(PacketType::CommittedProposalResponse);
    PBFTProposalList proposals = {proposal};
    responseMsg->setProposals(proposals);
    // Use the codec to produce the full PBFT packet format that decode() expects.
    auto encodedData = config->codec()->encode(responseMsg, config->pbftMsgDefaultVersion());

    auto logSync = std::make_shared<TestPBFTLogSync>(config, cacheProcessor);

    // Before: cache has no entry for blockNumber + 1.
    auto beforeSize = cacheProcessor->caches().size();

    auto sender = cryptoSuite->signatureImpl()->generateKeyPair()->publicKey();
    logSync->testOnRecvCommittedProposalsResponse(
        nullptr, sender, ref(*encodedData), currentBlockNumber + 1, 1);

    // After: cache must still have no new entry (proposal with no proofs must be dropped).
    BOOST_CHECK_EQUAL(cacheProcessor->caches().size(), beforeSize);
}

// FIB-127: A proposal with an insufficient quorum of valid signature proofs must be dropped.
BOOST_AUTO_TEST_CASE(testDropProposalWithInsufficientWeight)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    size_t consensusNodeSize = 4;
    size_t currentBlockNumber = 10;
    auto fakerMap =
        createFakers(cryptoSuite, consensusNodeSize, currentBlockNumber, consensusNodeSize);

    auto receiverFaker = fakerMap[0];
    auto config = receiverFaker->pbftConfig();
    auto cacheProcessor = std::dynamic_pointer_cast<FakeCacheProcessor>(
        receiverFaker->pbftEngine()->cacheProcessor());

    auto proposal = std::make_shared<PBFTProposal>();
    proposal->setIndex(static_cast<protocol::BlockNumber>(currentBlockNumber + 1));
    auto blockHash = hashImpl->hash(bytesConstRef("fib127-insufficient-weight"));
    proposal->setHash(blockHash);

    // Add only ONE valid sig proof (weight=1) while minRequiredQuorum for 4 nodes is 3.
    auto sigData = cryptoSuite->signatureImpl()->sign(*fakerMap[0]->keyPair(), blockHash);
    proposal->appendSignatureProof(0, ref(*sigData));

    auto responseMsg = std::make_shared<PBFTMessage>();
    responseMsg->setPacketType(PacketType::CommittedProposalResponse);
    responseMsg->setProposals({proposal});
    auto encodedData = config->codec()->encode(responseMsg, config->pbftMsgDefaultVersion());

    auto logSync = std::make_shared<TestPBFTLogSync>(config, cacheProcessor);

    auto beforeSize = cacheProcessor->caches().size();
    auto sender = cryptoSuite->signatureImpl()->generateKeyPair()->publicKey();
    logSync->testOnRecvCommittedProposalsResponse(
        nullptr, sender, ref(*encodedData), currentBlockNumber + 1, 1);

    // Insufficient quorum → proposal must be rejected.
    BOOST_CHECK_EQUAL(cacheProcessor->caches().size(), beforeSize);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
