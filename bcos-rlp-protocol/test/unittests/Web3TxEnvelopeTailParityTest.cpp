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
 * @file Web3TxEnvelopeTailParityTest.cpp
 * @brief classifyWeb3EnvelopeChainId legacy ListEnd parity (finding AW): the legacy
 * envelope is exactly 9 items — full (v, r, s) or preimage (chainId, 0, 0) — and any
 * other in-list tail must classify Malformed, matching what Web3TxHandler's decode
 * already rejects. A junk-tailed envelope used to classify from its first 7 items and
 * pass classifier-based admission gates it would later fail in decode.
 * @date 2026/8/28
 */

#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-rlp-protocol/Web3TxEnvelope.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <boost/test/unit_test.hpp>

namespace codec_rlp = bcos::codec::rlp;

namespace bcos::test
{
namespace
{
bcos::bytes concat(bcos::bytes base, bcos::bytes const& tail)
{
    base.insert(base.end(), tail.begin(), tail.end());
    return base;
}

bcos::bytes item(uint64_t value)
{
    bcos::bytes out;
    codec_rlp::encode(out, value);
    return out;
}

// Six legacy fields (nonce, gasPrice, gas, to, value, input) as zero placeholders.
bcos::bytes sixFields()
{
    bcos::bytes out;
    for (int i = 0; i < 6; ++i)
    {
        codec_rlp::encode(out, static_cast<uint64_t>(0));
    }
    return out;
}

// Assemble a legacy list envelope from already-encoded items, optionally appending
// extra raw bytes (items or junk) INSIDE the list payload, after the given items.
bcos::bytes legacyEnvelope(bcos::bytes items, bcos::bytes const& inListTail = {})
{
    items.insert(items.end(), inListTail.begin(), inListTail.end());
    bcos::bytes env;
    codec_rlp::encodeHeader(env, {.isList = true, .payloadLength = items.size()});
    env.insert(env.end(), items.begin(), items.end());
    return env;
}

bcos::rlp::protocol::Web3EnvelopeChainIdKind classify(bcos::bytes const& env)
{
    return bcos::rlp::protocol::classifyWeb3EnvelopeChainId(bcos::ref(env)).kind;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3TxEnvelopeTailParityTest)

// Clean full form (v >= 35): 6 fields + v + r + s, protected on chainId (v-35)>>1.
BOOST_AUTO_TEST_CASE(cleanFullFormStillProtected)
{
    BOOST_CHECK(
        classify(legacyEnvelope(concat(sixFields(), concat(item(37), concat(item(1), item(1)))))) ==
        bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
}

// Clean preimage tail: 6 fields + chainId + empty + empty, protected on the chainId.
BOOST_AUTO_TEST_CASE(cleanPreimageTailStillProtected)
{
    auto env = legacyEnvelope(
        concat(sixFields(), concat(item(1), concat(bcos::bytes{0x80}, bcos::bytes{0x80}))));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
}

// Clean v=27 full form stays exempt.
BOOST_AUTO_TEST_CASE(cleanUnprotectedStillExempt)
{
    BOOST_CHECK(
        classify(legacyEnvelope(concat(sixFields(), concat(item(27), concat(item(1), item(1)))))) ==
        bcos::rlp::protocol::Web3EnvelopeChainIdKind::Unprotected);
}

// AW: junk item appended after the full form's (v, r, s) must classify Malformed —
// previously the classifier ignored it while decode rejected the same bytes.
BOOST_AUTO_TEST_CASE(junkItemAfterFullFormIsMalformed)
{
    auto env =
        legacyEnvelope(concat(sixFields(), concat(item(37), concat(item(1), item(1)))), item(9));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// AW: junk item appended after the preimage tail's (chainId, 0, 0).
BOOST_AUTO_TEST_CASE(junkItemAfterPreimageTailIsMalformed)
{
    auto env = legacyEnvelope(
        concat(sixFields(), concat(item(1), concat(bcos::bytes{0x80}, bcos::bytes{0x80}))),
        item(9));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// AW: junk after an unprotected v=27 full form must not ride the exemption.
BOOST_AUTO_TEST_CASE(junkItemAfterUnprotectedIsMalformed)
{
    auto env =
        legacyEnvelope(concat(sixFields(), concat(item(27), concat(item(1), item(1)))), item(9));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// AW: a 7-item form (v only, no r/s) is not a legacy shape decode accepts.
BOOST_AUTO_TEST_CASE(missingSignatureItemsAreMalformed)
{
    BOOST_CHECK(classify(legacyEnvelope(concat(sixFields(), item(37)))) ==
                bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// AW: an 8-item form (v, r only) likewise.
BOOST_AUTO_TEST_CASE(oneSignatureItemMissingIsMalformed)
{
    BOOST_CHECK(classify(legacyEnvelope(concat(sixFields(), concat(item(37), item(1))))) ==
                bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// M8: the third trailing item must not be a LIST shape.
BOOST_AUTO_TEST_CASE(listShapedTrailingItemIsMalformed)
{
    auto env = legacyEnvelope(
        concat(sixFields(), concat(item(37), concat(item(1), item(1)))), bcos::bytes{0xc0});
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// AW: trailing junk whose header overruns the list payload fails closed.
BOOST_AUTO_TEST_CASE(overrunningTrailingItemIsMalformed)
{
    bcos::bytes overrun{0xb9, 0xff, 0xff};  // long-string header claiming 65535 bytes
    auto env =
        legacyEnvelope(concat(sixFields(), concat(item(37), concat(item(1), item(1)))), overrun);
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}


// S6: a sealed full form never carries an empty r/s item (EIP-2 keeps r,s in [1,n-1]) —
// exactly one empty item must classify Malformed, not ride the 27/28 exemption nor the
// 35+ protected band, matching what decode()'s sealed branch rejects.
BOOST_AUTO_TEST_CASE(oneEmptySignatureItemIsMalformed)
{
    auto emptyR =
        legacyEnvelope(concat(sixFields(), concat(item(27), concat(bcos::bytes{0x80}, item(1)))));
    BOOST_CHECK(classify(emptyR) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
    auto emptyS =
        legacyEnvelope(concat(sixFields(), concat(item(37), concat(item(1), bcos::bytes{0x80}))));
    BOOST_CHECK(classify(emptyS) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// 32-byte big-endian scalar item — real signature width. decodeHeader CONSUMES the 0xa0
// header when probing, unlike the inline item(1) forms the fixtures above use, so only
// these shapes exercise the emptySeen walk's cursor hygiene (kyonRay R3 #1/#2).
bcos::bytes wideScalar(bcos::byte fill)
{
    bcos::bytes out(1, 0xa0);
    out.insert(out.end(), 32, fill);
    return out;
}

// R3 #2: a real-width sealed envelope (v=38 = chainId 1, 32-byte r/s) must classify
// Protected with the chainId — the preimage probe consumes r's 0xa0 header, and the
// emptySeen walk must still re-anchor on the tail's item boundary, not parse r's interior
// and answer Malformed (the data-dependent aliasing defect). r's first payload byte is
// 0xc1 — a list header if the walk were anchored mid-r, which is what makes this fixture
// fail on the aliasing code.
BOOST_AUTO_TEST_CASE(sealedFullFormRealWidthSignatureIsProtected)
{
    bcos::bytes r = wideScalar(0x11);
    r[1] = 0xc1;  // >= 0xc0: parsed as a list header from inside r's payload
    auto env = legacyEnvelope(concat(sixFields(), concat(item(38), concat(r, wideScalar(0x22)))));
    auto const result = bcos::rlp::protocol::classifyWeb3EnvelopeChainId(bcos::ref(env));
    BOOST_CHECK(result.kind == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
    BOOST_CHECK_EQUAL(result.chainId, 1U);
}

// R4 #1: bytes AFTER the outer RLP list — the typed arm rejects the same shape
// (`!cursor.empty()`) and reassembleWeb3RawTransaction rejects it; the legacy arm must
// classify Malformed too instead of ignoring the tail (poisoning-window parity).
BOOST_AUTO_TEST_CASE(junkAfterOuterListIsMalformed)
{
    auto env = legacyEnvelope(concat(sixFields(), concat(item(37), concat(item(1), item(1)))));
    env.push_back(0xde);  // junk outside the list
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// R3 #2: the S6 exactly-one-empty rule pinned at real signature width — 32-byte r with an
// EMPTY s must classify Malformed, not ride the Protected band because the walk parsed
// r's interior and never saw the empty item.
BOOST_AUTO_TEST_CASE(oneEmptySignatureItemRealWidthIsMalformed)
{
    auto env = legacyEnvelope(
        concat(sixFields(), concat(item(38), concat(wideScalar(0x11), bcos::bytes{0x80}))));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// S6: the typed inner list must close at the envelope boundary — junk after the list is
// Malformed, matching decode()'s trailing-garbage rejection on the same bytes.
BOOST_AUTO_TEST_CASE(typedListTrailingGarbageIsMalformed)
{
    bcos::bytes items;
    for (int i = 0; i < 7; ++i)
    {
        codec_rlp::encode(items, static_cast<uint64_t>(i == 0 ? 1 : 0));  // field0 = chainId
    }
    bcos::bytes body;
    codec_rlp::encodeHeader(body, {.isList = true, .payloadLength = items.size()});
    body.insert(body.end(), items.begin(), items.end());
    bcos::bytes env{0x02};
    env.insert(env.end(), body.begin(), body.end());
    env.push_back(0xde);  // junk after the typed list
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
    bcos::bytes clean(env.begin(), env.end() - 1);
    BOOST_CHECK(classify(clean) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Protected);
}

// S9: canonical-integer grid on the classifier path — bare 0x00, leading-zero and
// over-wide integer items must classify Malformed (decodeCanonicalRlpUint rejects them).
BOOST_AUTO_TEST_CASE(nonCanonicalIntegerItemsAreMalformed)
{
    auto bareZero = legacyEnvelope(concat(
        sixFields(), concat(bcos::bytes{0x00}, concat(bcos::bytes{0x80}, bcos::bytes{0x80}))));
    BOOST_CHECK(classify(bareZero) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
    auto leadingZero = legacyEnvelope(concat(sixFields(),
        concat(bcos::bytes{0x82, 0x00, 0x01}, concat(bcos::bytes{0x80}, bcos::bytes{0x80}))));
    BOOST_CHECK(classify(leadingZero) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
    bcos::bytes wideV{0xa1};  // 0x80+33: 33-byte payload
    wideV.insert(wideV.end(), 33, 0x01);
    auto overWide = legacyEnvelope(concat(sixFields(), concat(wideV, concat(item(1), item(1)))));
    BOOST_CHECK(classify(overWide) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// S9: v=26 sits in the malformed Homestead band — never exempt, never protected.
BOOST_AUTO_TEST_CASE(v26HomesteadBandIsMalformed)
{
    auto env = legacyEnvelope(concat(sixFields(), concat(item(26), concat(item(1), item(1)))));
    BOOST_CHECK(classify(env) == bcos::rlp::protocol::Web3EnvelopeChainIdKind::Malformed);
}

// S9: checkEip2Signature boundary grid — each [1, n-1] / s <= n/2 edge needs a fixture so
// relaxing any single edge cannot pass silently.
BOOST_AUTO_TEST_CASE(eip2SignatureBoundaryGrid)
{
    namespace crypto = bcos::crypto;
    // Full-width 32-byte big-endian form of a u256 (left-padded with zeros).
    auto const be32 = [](bcos::u256 value) {
        bcos::bytes out(32, 0x00);
        for (int i = 31; i >= 0; --i)
        {
            out[i] = static_cast<bcos::byte>(static_cast<uint64_t>(value & 0xff));
            value >>= 8;
        }
        return out;
    };
    auto const rOne = be32(bcos::u256(1));
    auto const sOne = be32(bcos::u256(1));
    auto const sMax = be32(crypto::c_secp256k1nOver2);
    auto const sOver = be32(crypto::c_secp256k1nOver2 + 1);
    auto const rMax = be32(crypto::c_secp256k1n - 1);
    auto const rN = be32(crypto::c_secp256k1n);
    auto const sHigh = be32(crypto::c_secp256k1n - 1);
    auto const zero = bcos::bytes(32, 0x00);

    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rOne), bcos::ref(sOne)) == nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rOne), bcos::ref(sMax)) == nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rMax), bcos::ref(sOne)) == nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(zero), bcos::ref(sOne)) != nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rOne), bcos::ref(zero)) != nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rN), bcos::ref(sOne)) != nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rOne), bcos::ref(sOver)) != nullptr);
    BOOST_CHECK(bcos::checkEip2Signature(bcos::ref(rOne), bcos::ref(sHigh)) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
