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
 * @brief FIB-134 regression — packetType bound into PBFT signature digest
 * @file FIB134_PacketTypeSignatureTest.cpp
 */
#include "FakePBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTCodec.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessageFactoryImpl.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/PacketTypeDigest.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace std::string_view_literals;

namespace bcos::test
{
namespace
{
// Build a CryptoSuite + KeyPair + PBFTCodec triplet for the round-trip tests.
struct CodecHarness
{
    CryptoSuite::Ptr cryptoSuite;
    KeyPairInterface::Ptr keyPair;
    PBFTMessageFactory::Ptr factory;
    PBFTCodec::Ptr codec;
};

inline CodecHarness makeHarness()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    KeyPairInterface::Ptr keyPair = signatureImpl->generateKeyPair();
    PBFTMessageFactory::Ptr factory = std::make_shared<PBFTMessageFactoryImpl>();
    auto codec = std::make_shared<PBFTCodec>(keyPair, cryptoSuite, factory);
    return {std::move(cryptoSuite), std::move(keyPair), std::move(factory), std::move(codec)};
}

// Build a PBFTMessage (PrePrepare/Prepare/Commit/Checkpoint family) with the
// requested wire-version on the inner BaseMessage, ready to feed into PBFTCodec::encode.
inline PBFTMessage::Ptr makeInnerPBFTMessage(
    CodecHarness const& h, int32_t version, PacketType packetType)
{
    auto fixture = std::make_shared<PBFTMessageFixture>(h.cryptoSuite, h.keyPair);
    auto proposalHash = h.cryptoSuite->hashImpl()->hash("FIB134-inner"sv);
    BlockNumber index = 4242;
    std::string dataStr{"FIB134-inner-data"};
    bytes data(dataStr.begin(), dataStr.end());

    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> nodeKeyPairList;
    for (size_t i = 0; i < 4; ++i)
    {
        nodeKeyPairList.emplace_back(
            static_cast<int64_t>(i), h.cryptoSuite->signatureImpl()->generateKeyPair());
    }
    auto proposals = fakeProposals(h.cryptoSuite, fixture, nodeKeyPairList, index, data, 2);
    auto msg = fixture->fakePBFTMessage(
        utcTime(), version, /*view=*/7, /*generatedFrom=*/0, proposalHash, proposals);
    msg->setIndex(index);
    msg->setPacketType(packetType);
    return msg;
}

// Build a PBFTViewChangeMsg wired with the requested version, ready for codec round-trip.
inline PBFTViewChangeMsg::Ptr makeViewChangeMessage(CodecHarness const& h, int32_t version)
{
    auto fixture = std::make_shared<PBFTMessageFixture>(h.cryptoSuite, h.keyPair);
    auto proposalHash = h.cryptoSuite->hashImpl()->hash("FIB134-vc"sv);
    BlockNumber index = 4243;
    BlockNumber committedIndex = 4242;
    std::string dataStr{"FIB134-vc-data"};
    bytes data(dataStr.begin(), dataStr.end());
    auto committedHash = h.cryptoSuite->hash(std::to_string(committedIndex));
    return fakeViewChangeMessage(utcTime(), version, /*view=*/9, /*generatedFrom=*/0, proposalHash,
        index, data, committedIndex, committedHash, 2, fixture);
}

// Mutate the on-wire `RawMessage.type` field (outer packetType) to a different value,
// emulating an attacker re-routing a signed payload to a different handler.
inline bytes tamperWirePacketType(bytesPointer const& encoded, PacketType newType)
{
    auto raw = std::make_shared<RawMessage>();
    bcos::protocol::decodePBObject(raw, bytesConstRef(encoded->data(), encoded->size()));
    raw->set_type(static_cast<int32_t>(newType));
    auto reEncoded = bcos::protocol::encodePBObject(raw);
    return bytes(reEncoded->begin(), reEncoded->end());
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB134_PacketTypeSignature, TestPromptFixture)

// Direct helper-level proof: under v=1 the digest is bound to packetType.
BOOST_AUTO_TEST_CASE(helper_v1_packetType_changes_digest)
{
    auto hashImpl = std::make_shared<Keccak256>();
    bytes payload{0x01, 0x02, 0x03, 0x04, 0x05};
    auto payloadRef = bytesConstRef(payload.data(), payload.size());

    auto digestPrePrepare = PacketTypeDigest::outer(
        /*version=*/1, static_cast<int32_t>(PacketType::PrePreparePacket), payloadRef, hashImpl);
    auto digestCommit = PacketTypeDigest::outer(
        /*version=*/1, static_cast<int32_t>(PacketType::CommitPacket), payloadRef, hashImpl);
    BOOST_CHECK(digestPrePrepare != digestCommit);

    auto innerPP = PacketTypeDigest::inner(
        /*version=*/1, static_cast<int32_t>(PacketType::PrePreparePacket), payloadRef, hashImpl);
    auto innerCommit = PacketTypeDigest::inner(
        /*version=*/1, static_cast<int32_t>(PacketType::CommitPacket), payloadRef, hashImpl);
    BOOST_CHECK(innerPP != innerCommit);

    // v=0 legacy: digest must be independent of packetType
    auto legacyPP = PacketTypeDigest::outer(
        /*version=*/0, static_cast<int32_t>(PacketType::PrePreparePacket), payloadRef, hashImpl);
    auto legacyCommit = PacketTypeDigest::outer(
        /*version=*/0, static_cast<int32_t>(PacketType::CommitPacket), payloadRef, hashImpl);
    BOOST_CHECK_EQUAL(legacyPP.hex(), legacyCommit.hex());
}

// v=1 inner-path tamper: an attacker flips the wire packetType from Commit to PrePrepare.
// Both packet types decode through createPBFTMsg, so the structural decode succeeds, but
// the receiver's recomputed inner digest differs (since packetType is now bound), so the
// inner signature verification must fail.
BOOST_AUTO_TEST_CASE(v1_inner_packetType_tampered_rejected)
{
    auto h = makeHarness();
    auto msg = makeInnerPBFTMessage(h, /*version=*/1, PacketType::CommitPacket);
    auto encoded = h.codec->encode(msg, /*outer-version=*/1);

    auto tamperedBytes = tamperWirePacketType(encoded, PacketType::PrePreparePacket);
    auto decoded = h.codec->decode(bytesConstRef(tamperedBytes.data(), tamperedBytes.size()));
    BOOST_REQUIRE(decoded != nullptr);
    BOOST_CHECK_EQUAL(decoded->packetType(), PacketType::PrePreparePacket);
    // Inner-path verification (used for PrePrepare/Prepare/Commit/CheckPoint/Recover*).
    BOOST_CHECK(decoded->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == false);
}

// v=1 outer-path tamper: a ViewChange (which DOES go through the outer wrapper signature)
// gets its wire packetType replaced with NewViewPacket — the only other packet type that
// also runs through `shouldHandleSignature`, ensuring the receiver still reaches the
// outer-sig branch and recomputes the digest using the tampered packetType under v=1.
// The stored signature was computed against ViewChange's packetType, so verify must fail.
BOOST_AUTO_TEST_CASE(v1_outer_packetType_tampered_rejected)
{
    auto h = makeHarness();
    auto vc = makeViewChangeMessage(h, /*version=*/1);
    auto encoded = h.codec->encode(vc, /*outer-version=*/1);

    auto tamperedBytes = tamperWirePacketType(encoded, PacketType::NewViewPacket);
    PBFTBaseMessageInterface::Ptr decoded;
    try
    {
        decoded = h.codec->decode(bytesConstRef(tamperedBytes.data(), tamperedBytes.size()));
    }
    catch (...)
    {
        // Decode itself may throw because the wire payload is ViewChange-shaped while
        // the factory expects NewView. Treat that as rejection.
        BOOST_TEST_PASSPOINT();
        return;
    }
    BOOST_REQUIRE(decoded != nullptr);
    // Outer-path verification: the receiver recomputes the digest using the tampered
    // packetType (NewView) under v=1, but the stored signature was sealed for the
    // original packetType (ViewChange) — so verify must fail.
    BOOST_CHECK(decoded->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == false);
}

// v=0 legacy accept: a node that emits v=0 traffic (or replays a historical message)
// must continue to verify under the legacy formula `hash(payload)`.
BOOST_AUTO_TEST_CASE(v0_legacy_message_accepted)
{
    auto h = makeHarness();
    auto msg = makeInnerPBFTMessage(h, /*version=*/0, PacketType::CommitPacket);
    auto encoded = h.codec->encode(msg, /*outer-version=*/0);

    auto decoded = h.codec->decode(bytesConstRef(encoded->data(), encoded->size()));
    BOOST_REQUIRE(decoded != nullptr);
    BOOST_CHECK_EQUAL(decoded->packetType(), PacketType::CommitPacket);
    BOOST_CHECK_EQUAL(decoded->version(), 0);
    BOOST_CHECK(decoded->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);

    // ViewChange under v=0 too — outer-wrapper path on the legacy formula.
    auto vc = makeViewChangeMessage(h, /*version=*/0);
    auto vcEncoded = h.codec->encode(vc, /*outer-version=*/0);
    auto vcDecoded = h.codec->decode(bytesConstRef(vcEncoded->data(), vcEncoded->size()));
    BOOST_REQUIRE(vcDecoded != nullptr);
    BOOST_CHECK(vcDecoded->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);
}

// Dual-mode receiver: same codec instance must verify both v=0 and v=1 traffic.
BOOST_AUTO_TEST_CASE(dual_mode_receiver_v0_and_v1_both_verify)
{
    auto h = makeHarness();

    auto msgV0 = makeInnerPBFTMessage(h, /*version=*/0, PacketType::CommitPacket);
    auto encodedV0 = h.codec->encode(msgV0, /*outer-version=*/0);
    auto decodedV0 = h.codec->decode(bytesConstRef(encodedV0->data(), encodedV0->size()));
    BOOST_REQUIRE(decodedV0 != nullptr);
    BOOST_CHECK_EQUAL(decodedV0->version(), 0);
    BOOST_CHECK(decodedV0->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);

    auto msgV1 = makeInnerPBFTMessage(h, /*version=*/1, PacketType::CommitPacket);
    auto encodedV1 = h.codec->encode(msgV1, /*outer-version=*/1);
    auto decodedV1 = h.codec->decode(bytesConstRef(encodedV1->data(), encodedV1->size()));
    BOOST_REQUIRE(decodedV1 != nullptr);
    BOOST_CHECK_EQUAL(decodedV1->version(), 1);
    BOOST_CHECK(decodedV1->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);

    // Outer path dual-mode (ViewChange).
    auto vcV0 = makeViewChangeMessage(h, /*version=*/0);
    auto vcEncodedV0 = h.codec->encode(vcV0, /*outer-version=*/0);
    auto vcDecodedV0 = h.codec->decode(bytesConstRef(vcEncodedV0->data(), vcEncodedV0->size()));
    BOOST_REQUIRE(vcDecodedV0 != nullptr);
    BOOST_CHECK(vcDecodedV0->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);

    auto vcV1 = makeViewChangeMessage(h, /*version=*/1);
    auto vcEncodedV1 = h.codec->encode(vcV1, /*outer-version=*/1);
    auto vcDecodedV1 = h.codec->decode(bytesConstRef(vcEncodedV1->data(), vcEncodedV1->size()));
    BOOST_REQUIRE(vcDecodedV1 != nullptr);
    BOOST_CHECK(vcDecodedV1->verifySignature(h.cryptoSuite, h.keyPair->publicKey()) == true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
