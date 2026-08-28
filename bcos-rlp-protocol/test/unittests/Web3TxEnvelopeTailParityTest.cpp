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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
