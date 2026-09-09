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
 * @file Web3AuthorizationDecodeTest.cpp
 * @brief EIP-7702 authorization-entry decode: op-geth's uint8 yParity domain.
 */

#include <bcos-rlp-protocol/Web3Transaction.h>
#include <boost/test/unit_test.hpp>
#include <array>

using namespace bcos;

namespace bcos::test
{
namespace
{
/// RLP-encode one authorization entry [chain_id, address, nonce, <yParity item>, r, s] with an
/// explicit yParity item so a test can pin the exact wire form.
bytes makeAuthEntry(std::span<byte const> yParityItem)
{
    bytes payload;
    codec::rlp::encode(payload, u256(1));
    std::array<byte, 20> addressBytes{};
    codec::rlp::encode(payload, bytesConstRef{addressBytes.data(), addressBytes.size()});
    codec::rlp::encode(payload, uint64_t{7});
    payload.insert(payload.end(), yParityItem.begin(), yParityItem.end());
    codec::rlp::encode(payload, u256(1));
    codec::rlp::encode(payload, u256(1));

    bytes out;
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::optional<rpc::AuthorizationListEntry> decodeEntry(
    bytes const& entry, bytesRef& walker, std::string* error = nullptr)
{
    walker = bytesRef(const_cast<byte*>(entry.data()), entry.size());
    rpc::AuthorizationListEntry out{};
    if (auto err = codec::rlp::decode(walker, out); err != nullptr)
    {
        if (error != nullptr)
        {
            *error = err->errorMessage();
        }
        return std::nullopt;
    }
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3AuthorizationDecodeTest)

BOOST_AUTO_TEST_CASE(yParityAboveOneDecodesAndIsSkippedAtExecution)
{
    // op-geth decodes V as a plain uint8 and skips a V ∉ {0,1} authorization at execution, so
    // the entry must decode (value 2) instead of failing the whole transaction.
    auto const entry = makeAuthEntry(bytes{0x02});
    bytesRef walker;
    auto const decoded = decodeEntry(entry, walker);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->yParity, 2);
    BOOST_CHECK(walker.empty());
}

BOOST_AUTO_TEST_CASE(legacyWideParityValueDecodesAsUint8)
{
    // 27 (0x1b) is a legal uint8 V; execution skips it because it is not 0/1. It must decode
    // as a value, not fail the transaction.
    auto const entry = makeAuthEntry(bytes{0x1b});
    bytesRef walker;
    auto const decoded = decodeEntry(entry, walker);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->yParity, 27);
}

BOOST_AUTO_TEST_CASE(zeroAndOneStillDecode)
{
    for (auto const& [item, expected] :
        {std::pair{bytes{0x80}, uint8_t{0}}, std::pair{bytes{0x01}, uint8_t{1}}})
    {
        auto const entry = makeAuthEntry(item);
        bytesRef walker;
        auto const decoded = decodeEntry(entry, walker);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK_EQUAL(decoded->yParity, expected);
    }
}

BOOST_AUTO_TEST_CASE(nonCanonicalYParityStillRejected)
{
    // Bare 0x00 (leading zero), a multi-byte payload, and the 0x81 XX long form are rejected
    // by the shared canonical-RLP decoder (NonCanonicalSize). geth's decoder would accept the
    // last one, so this is a deliberate, documented strictness of FISCO's RLP layer rather
    // than a per-field rule: it applies to every integer field in every envelope.
    for (auto const& item : {bytes{0x00}, bytes{0x82, 0x00, 0x02}, bytes{0x81, 0x1b}})
    {
        auto const entry = makeAuthEntry(item);
        bytesRef walker;
        BOOST_CHECK(!decodeEntry(entry, walker).has_value());
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
