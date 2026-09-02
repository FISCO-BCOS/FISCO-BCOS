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
 * @file ExtraTxBytesDualLayoutTest.cpp
 * @brief decodeFromPayload must accept BOTH extraTransactionBytes layouts
 *
 * extraTransactionBytes changes form over the transaction lifecycle: the txpool stage
 * stores the signing preimage (takeToTarsTransaction → encodeForSign), while sealed OP
 * blocks overwrite it with the full wire envelope (buildOpBlock, for L1 pricing). The
 * no-sig decode used by the RPC readback must therefore discriminate — legacy via the
 * trailer shape ((chainId,0,0) vs (v,r,s)), typed via the fixed field count (N vs N+3).
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
// Signing preimage of legacy_eip155_chain1 (chainId=1) and the canonical wire envelope it
// assembles to with its signature; keccak256(wire) anchors the pair (New1 test vectors).
constexpr std::string_view LEGACY_PREIMAGE_HEX =
    "ee07847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef01808"
    "0";
constexpr std::string_view LEGACY_WIRE_HEX =
    "f86e07847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef"
    "26a094f5e0158ba372a7b451996ac591fec2055d7ca7d0f72aa6b473d7b84a69d582a062a22960ade9f059d3894"
    "89fff300213df3ef0e09e7b4ab89508b934530c5661";
constexpr std::string_view LEGACY_WIRE_HASH_HEX =
    "c063947c632cfcf52d7a11c9e8887430bcfb673b2bfc33ddc6d961ba77ffa2f4";

constexpr std::string_view EIP1559_PREIMAGE_HEX =
    "02f20507843b9aca00847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c68000"
    "84deadbeefc0";
constexpr std::string_view EIP1559_WIRE_HEX =
    "02f8750507843b9aca00847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c68"
    "00084deadbeefc001a03dc2e61019aa7824a7e290f2b097d10a405e989fe4d3348268df2afcf647b408a06e0208"
    "4295955b070394ea8e604d86b4942e4a1638358dca984e9f6adff46d4a";

// pre-EIP-155 preimage: 6 items, no trailer.
constexpr std::string_view LEGACY_PRE155_PREIMAGE_HEX =
    "eb07847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef";

Web3Transaction decodeNoSig(std::string_view hex)
{
    auto bytes = fromHex(hex);
    auto ref = bcos::ref(bytes);
    Web3Transaction tx{};
    auto error = codec::rlp::decodeFromPayload(ref, tx);
    BOOST_REQUIRE(error == nullptr);
    // The dispatcher's trailing-bytes check must also have passed (ref fully consumed).
    BOOST_REQUIRE(ref.empty());
    return tx;
}

Web3Transaction decodeNoSigBytes(bcos::bytes bytes)
{
    auto ref = bcos::ref(bytes);
    Web3Transaction tx{};
    auto error = codec::rlp::decodeFromPayload(ref, tx);
    BOOST_REQUIRE(error == nullptr);
    BOOST_REQUIRE(ref.empty());
    return tx;
}

// Shared typed-tx body for dual-layout probes. Signatures are structurally valid
// (yParity 0/1, 32-byte r/s below n/2) so leftover EIP-2 does not mask the layout.
Web3Transaction makeTypedDualLayoutTx(rpc::TransactionType type, uint64_t chainId)
{
    Web3Transaction tx;
    tx.type = type;
    tx.chainId = chainId;
    tx.nonce = 7;
    tx.maxPriorityFeePerGas = 1'000'000'000;
    tx.maxFeePerGas = 2'000'000'000;
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    tx.value = 0;
    tx.data = {};
    tx.signatureV = 1;
    tx.signatureR.assign(32, static_cast<bcos::byte>(0x11));
    tx.signatureS.assign(32, static_cast<bcos::byte>(0x22));
    return tx;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(extraTxBytesDualLayout, RPCFixture)

// Legacy signing preimage ((chainId,0,0) trailer) — the txpool-stage layout. chainId comes
// from item 7 and the whole trailer is consumed (the old single-item read left the 0,0
// placeholders unconsumed and tripped the trailing-bytes check).
BOOST_AUTO_TEST_CASE(legacyPreimageDecodesChainId)
{
    auto tx = decodeNoSig(LEGACY_PREIMAGE_HEX);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1U);
    BOOST_CHECK_EQUAL(tx.nonce, 7U);
}

// Legacy wire envelope ((v,r,s) trailer) — the sealed-block layout. This is the C2
// regression: v used to land in the chainId slot (chainId=38 instead of 1).
BOOST_AUTO_TEST_CASE(legacyWireDecodesChainId)
{
    auto tx = decodeNoSig(LEGACY_WIRE_HEX);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1U);
}

// R3 #1/#2: the PR's own signed, hash-anchored legacy sealed vector must classify
// Protected with chainId 1 — this is the exact vector whose r (first byte 0x94, with a
// 0xf7 at r[21]) made the pre-fix emptySeen walk parse r's interior and answer Malformed.
BOOST_AUTO_TEST_CASE(legacyWireClassifiesProtectedChainId)
{
    auto const classified =
        bcos::rlp::protocol::classifyWeb3EnvelopeChainId(bcos::ref(fromHex(LEGACY_WIRE_HEX)));
    BOOST_CHECK(classified.kind == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
    BOOST_CHECK_EQUAL(classified.chainId, 1U);
}

// Pre-EIP-155 6-item preimage: no trailer at all → chainId stays nullopt.
BOOST_AUTO_TEST_CASE(legacyPre155PreimageNoChainId)
{
    auto tx = decodeNoSig(LEGACY_PRE155_PREIMAGE_HEX);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_CHECK(!tx.chainId.has_value());
}

// A trailer that is neither (chainId,0,0) nor a valid (v,r,s): v=5 with a non-empty s defeats
// the preimage-tail discriminator (isLegacyPreimageTail = r and s both empty), so the sealed
// branch runs and item7=5 < 35 must reject (InvalidVInSignature).
BOOST_AUTO_TEST_CASE(legacyGarbageTrailerRejected)
{
    // EIP-155 preimage with the (chainId=1, r=empty, s=empty) trailer rewritten as
    // (v=5, r=empty, s=0x01): same 3-byte payload, no length drift.
    std::string hex{LEGACY_PREIMAGE_HEX};
    hex.replace(hex.size() - 6, 6, "058001");
    auto bytes = fromHex(hex);
    auto ref = bcos::ref(bytes);
    Web3Transaction tx{};
    auto error = codec::rlp::decodeFromPayload(ref, tx);
    BOOST_REQUIRE(error != nullptr);
    // Pin the cause class: the failure must be the v<35 band
    // (InvalidVInSignature), not a generic or trailing-bytes error — a regression that
    // moves the rejection cause would otherwise pass this null-check unnoticed.
    BOOST_CHECK(
        error->errorCode() == static_cast<int>(codec::rlp::DecodingError::InvalidVInSignature));
}

// EIP-1559 signing preimage (9 items) — txpool-stage layout.
BOOST_AUTO_TEST_CASE(typedPreimageDecodes)
{
    auto tx = decodeNoSig(EIP1559_PREIMAGE_HEX);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
}

// EIP-1559 wire envelope (12 items) — sealed-block layout. The trailing (yParity,r,s)
// must be consumed so the dispatcher's trailing-bytes check passes.
BOOST_AUTO_TEST_CASE(typedWireDecodes)
{
    auto tx = decodeNoSig(EIP1559_WIRE_HEX);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
}

// End-to-end hash consistency: withSig decode of the wire envelope re-encodes to the same
// bytes (canonical RLP round trip), so the derived txHash matches the canonical hash —
// the invariant op-node's PayloadByHash depends on.
BOOST_AUTO_TEST_CASE(wireDecodeReencodesToCanonicalHash)
{
    auto bytes = fromHex(LEGACY_WIRE_HEX);
    auto ref = bcos::ref(bytes);
    Web3Transaction tx{};
    auto error = codec::rlp::decode(ref, tx);  // withSig = true
    BOOST_REQUIRE(error == nullptr);
    BOOST_REQUIRE(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1U);
    BOOST_CHECK_EQUAL(tx.getSignatureV(), 38U);  // 1*2 + 35 + parity 1
    auto const reencoded = tx.encode();
    BOOST_CHECK(reencoded == bytes);
    BOOST_CHECK_EQUAL(tx.txHash(), HashType(fromHex(LEGACY_WIRE_HASH_HEX)));
}

// Matrix: T01 — EIP-2930 (0x01) preimage and sealed wire both survive decodeFromPayload.
// Finding G: ExtraTx historically only pinned 0x02.
BOOST_AUTO_TEST_CASE(eip2930PreimageAndWireDecodes)
{
    auto const built = makeTypedDualLayoutTx(rpc::TransactionType::EIP2930, 5);
    {
        auto const tx = decodeNoSigBytes(built.encodeForSign());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP2930);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
    }
    {
        auto const tx = decodeNoSigBytes(built.encode());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP2930);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
    }
}

// Matrix: T02 — EIP-7702 (0x04) preimage and sealed wire both survive decodeFromPayload.
BOOST_AUTO_TEST_CASE(eip7702PreimageAndWireDecodes)
{
    auto const built = makeTypedDualLayoutTx(rpc::TransactionType::EIP7702, 5);
    {
        auto const tx = decodeNoSigBytes(built.encodeForSign());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP7702);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
    }
    {
        auto const tx = decodeNoSigBytes(built.encode());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP7702);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
    }
}

// Matrix: T11 — EIP-4844 (0x03) preimage and sealed wire must decodeFromPayload
// without leftover InputTooLong (pool still rejects blobs; this is the decoder layout).
BOOST_AUTO_TEST_CASE(eip4844PreimageAndWireDecodes)
{
    auto built = makeTypedDualLayoutTx(rpc::TransactionType::EIP4844, 5);
    built.maxFeePerBlobGas = 1;
    built.blobVersionedHashes.emplace_back(
        HashType("abababababababababababababababababababababababababababababababab"));
    {
        auto const tx = decodeNoSigBytes(built.encodeForSign());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
        BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 1U);
    }
    {
        auto const tx = decodeNoSigBytes(built.encode());
        BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
        BOOST_REQUIRE(tx.chainId.has_value());
        BOOST_CHECK_EQUAL(tx.chainId.value(), 5U);
        BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 1U);
    }
}

// Leading-zero r as a byte string (0x82 0x00 0x01) is non-canonical integer RLP and must
// be rejected at decodeFromPayload. Real envelopes encode r/s as minimal integers.
BOOST_AUTO_TEST_CASE(typedLeadingZeroRRejected)
{
    namespace rlp = bcos::codec::rlp;
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(1));
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, static_cast<uint64_t>(1));
    rlp::encode(items, static_cast<uint64_t>(1));
    rlp::encode(items, static_cast<uint64_t>(21000));
    rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, bcos::bytes{});
    items.push_back(rlp::LIST_HEAD_BASE);
    rlp::encode(items, static_cast<uint64_t>(0));
    items.push_back(0x82);
    items.push_back(0x00);
    items.push_back(0x01);
    rlp::encode(items, bcos::bytes(32, 0x02));

    bcos::bytes envelope;
    envelope.push_back(static_cast<byte>(rpc::TransactionType::EIP1559));
    rlp::encodeHeader(envelope, rlp::Header{.isList = true, .payloadLength = items.size()});
    envelope.insert(envelope.end(), items.begin(), items.end());

    auto cursor = bcos::ref(envelope);
    Web3Transaction tx{};
    auto err = codec::rlp::decodeFromPayload(cursor, tx);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), static_cast<int>(rlp::DecodingError::NonCanonicalSize));
}

// Minimal typed envelopes (type || rlp([chainId])) are enough for classifyWeb3EnvelopeChainId,
// which sendRawTransaction and validateChainId both use.
BOOST_AUTO_TEST_CASE(typedMinimalEnvelopeClassifyChainId)
{
    namespace rlp = bcos::codec::rlp;
    auto make = [](uint8_t type, uint64_t chainId) {
        bcos::bytes fields;
        rlp::encode(fields, chainId);
        bcos::bytes wire;
        wire.push_back(type);
        rlp::encodeHeader(wire, rlp::Header{.isList = true, .payloadLength = fields.size()});
        wire.insert(wire.end(), fields.begin(), fields.end());
        return wire;
    };
    for (uint8_t const type : {uint8_t{0x01}, uint8_t{0x02}, uint8_t{0x04}})
    {
        auto const wire = make(type, 1);
        auto const classified = bcos::rlp::protocol::classifyWeb3EnvelopeChainId(bcos::ref(wire));
        BOOST_CHECK(classified.kind == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
        BOOST_CHECK_EQUAL(classified.chainId, 1U);
    }
    auto const mismatch = make(0x02, 999);
    auto const classified = bcos::rlp::protocol::classifyWeb3EnvelopeChainId(bcos::ref(mismatch));
    BOOST_CHECK(classified.kind == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
    BOOST_CHECK_EQUAL(classified.chainId, 999U);
}

// Legacy sealed withSig=false must use the same canonical r/s funnel as the withSig sibling.
BOOST_AUTO_TEST_CASE(legacySealedLeadingZeroRRejected)
{
    namespace rlp = bcos::codec::rlp;
    bcos::bytes items;
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, static_cast<uint64_t>(1));
    rlp::encode(items, static_cast<uint64_t>(21000));
    rlp::encode(items, bcos::Address("0x1111111111111111111111111111111111111111"));
    rlp::encode(items, static_cast<uint64_t>(0));
    rlp::encode(items, bcos::bytes{});
    rlp::encode(items, static_cast<uint64_t>(37));
    items.push_back(0x82);
    items.push_back(0x00);
    items.push_back(0x01);
    rlp::encode(items, bcos::bytes(32, 0x02));

    bcos::bytes envelope;
    rlp::encodeHeader(envelope, rlp::Header{.isList = true, .payloadLength = items.size()});
    envelope.insert(envelope.end(), items.begin(), items.end());

    auto cursor = bcos::ref(envelope);
    Web3Transaction tx{};
    auto err = codec::rlp::decodeFromPayload(cursor, tx);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), static_cast<int>(rlp::DecodingError::NonCanonicalSize));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
