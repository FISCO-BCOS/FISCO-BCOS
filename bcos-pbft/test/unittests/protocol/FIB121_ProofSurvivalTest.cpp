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
 * @brief FIB-121 characterization net: signature proofs appended to a
 *        consensusProposal AFTER it is set into a message must survive encode.
 *        This pins the write-through behaviour the ownership refactor must keep:
 *        a copy-at-set design that snapshots the proposal would silently drop
 *        proofs added later (the PBFTCache::intoPrecommit -> setSignatureList
 *        pattern, PBFTCache.cpp:172), so this test is the regression guard.
 * @file FIB121_ProofSurvivalTest.cpp
 * @date 2026-06-23
 */
#include "FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace std::string_view_literals;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB121ProofSurvivalTest, TestPromptFixture)

static CryptoSuite::Ptr makeSuite()
{
    return std::make_shared<CryptoSuite>(
        std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
}

// A consensus proposal carrying block data but NO signature proofs yet.
static PBFTProposal::Ptr makeBareProposal(
    CryptoSuite::Ptr const& _suite, BlockNumber _index, bytes const& _data)
{
    auto proposal = std::make_shared<PBFTProposal>();
    proposal->setIndex(_index);
    proposal->setHash(_suite->hashImpl()->hash("proposal"sv));
    proposal->setData(_data);
    return proposal;
}

// §0.2 (direct): append proofs AFTER setConsensusProposal, then encode the
// message itself. The proofs must be in the re-decoded message.
BOOST_AUTO_TEST_CASE(mutateAfterSetReflectedAtEncode)
{
    auto suite = makeSuite();
    KeyPairInterface::Ptr keyPair = suite->signatureImpl()->generateKeyPair();
    bytes data{'b', 'l', 'o', 'c', 'k', 'd', 'a', 't', 'a'};

    auto proposal = makeBareProposal(suite, 100, data);
    BOOST_CHECK(proposal->signatureProofSize() == 0);

    auto prePrepare = std::make_shared<PBFTMessage>();
    prePrepare->setPacketType(PacketType::PrePreparePacket);
    prePrepare->setVersion(1);
    prePrepare->setView(1);
    prePrepare->setIndex(100);
    prePrepare->setHash(proposal->hash());
    prePrepare->setConsensusProposal(proposal);

    // mutate AFTER set (the intoPrecommit / setSignatureList pattern)
    bytes sig0{'s', '0'};
    bytes sig1{'s', '1'};
    prePrepare->consensusProposal()->appendSignatureProof(7, ref(sig0));
    prePrepare->consensusProposal()->appendSignatureProof(9, ref(sig1));
    BOOST_CHECK(prePrepare->consensusProposal()->signatureProofSize() == 2);

    auto encoded = prePrepare->encode(suite, keyPair);
    auto decoded = std::make_shared<PBFTMessage>(suite, ref(*encoded));

    BOOST_REQUIRE(decoded->consensusProposal() != nullptr);
    BOOST_CHECK_EQUAL(decoded->consensusProposal()->signatureProofSize(), 2U);
    BOOST_CHECK_EQUAL(decoded->consensusProposal()->signatureProof(0).first, 7);
    BOOST_CHECK_EQUAL(decoded->consensusProposal()->signatureProof(1).first, 9);
    BOOST_CHECK(decoded->consensusProposal()->signatureProof(0).second.toBytes() == sig0);
    BOOST_CHECK(decoded->consensusProposal()->signatureProof(1).second.toBytes() == sig1);
    BOOST_CHECK(decoded->consensusProposal()->data().toBytes() == data);
}

// §0.2 (embedded): the prePrepare whose proposal was mutated-after-set is then
// embedded into a ViewChange and encoded. The proofs must survive the nested
// embedding round-trip — this is the path that fails under copy-at-set.
BOOST_AUTO_TEST_CASE(proofSurvivesViewChangeEmbedding)
{
    auto suite = makeSuite();
    bytes data{'p', 'a', 'y', 'l', 'o', 'a', 'd'};

    auto proposal = makeBareProposal(suite, 200, data);
    auto prePrepare = std::make_shared<PBFTMessage>();
    prePrepare->setPacketType(PacketType::PrePreparePacket);
    prePrepare->setVersion(1);
    prePrepare->setView(2);
    prePrepare->setIndex(200);
    prePrepare->setHash(proposal->hash());
    prePrepare->setConsensusProposal(proposal);

    // mutate after set
    bytes sig{'z', 'z'};
    prePrepare->consensusProposal()->appendSignatureProof(3, ref(sig));
    BOOST_CHECK(prePrepare->consensusProposal()->signatureProofSize() == 1);

    auto committed = makeBareProposal(suite, 199, bytes{});

    auto viewChange = std::make_shared<PBFTViewChangeMsg>();
    viewChange->setPacketType(PacketType::ViewChangePacket);
    viewChange->setVersion(1);
    viewChange->setView(2);
    viewChange->setIndex(200);
    viewChange->setHash(proposal->hash());
    viewChange->setCommittedProposal(committed);
    PBFTMessageList prepared{prePrepare};
    viewChange->setPreparedProposals(prepared);

    auto encoded = viewChange->encode(nullptr, nullptr);
    auto decodedViewChange = std::make_shared<PBFTViewChangeMsg>(ref(*encoded));

    BOOST_REQUIRE_EQUAL(decodedViewChange->preparedProposals().size(), 1U);
    auto decodedPrePrepare = decodedViewChange->preparedProposals()[0];
    BOOST_REQUIRE(decodedPrePrepare->consensusProposal() != nullptr);
    BOOST_CHECK_EQUAL(decodedPrePrepare->consensusProposal()->signatureProofSize(), 1U);
    BOOST_CHECK_EQUAL(decodedPrePrepare->consensusProposal()->signatureProof(0).first, 3);
    BOOST_CHECK(decodedPrePrepare->consensusProposal()->signatureProof(0).second.toBytes() == sig);
    BOOST_CHECK(decodedPrePrepare->consensusProposal()->data().toBytes() == data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
