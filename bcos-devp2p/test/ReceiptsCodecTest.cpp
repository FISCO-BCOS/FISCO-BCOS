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
 * @file ReceiptsCodecTest.cpp
 * @brief GetReceipts/Receipts codecs across eth/68 (trie form, bloom), eth/69 (EIP-7642
 *        bloom-less 4-item receipts) and eth/70 (EIP-7975 partial lists), pinned to the
 *        go-ethereum protocol_test.go vectors where they exist.
 * @date 2026/11/3
 */
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-rlp-protocol/EthLog.h>
#include <bcos-rlp-protocol/EthReceipt.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <deque>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(ReceiptsCodecTest)

namespace
{
// bcos::ref refuses temporaries; park decoded hex in a deque (stable addresses) so a
// test line can write hexRef("...") where a decoder wants a bytesConstRef.
bytesConstRef hexRef(std::string const& _hex)
{
    static std::deque<bcos::bytes> pool;
    pool.push_back(fromHex(_hex));
    return ref(pool.back());
}
h256 hashFromShortHex(std::string const& _hex)
{
    // geth common.HexToHash: left-pads the hex to 32 bytes.
    std::string padded(64 - _hex.size(), '0');
    padded += _hex;
    return h256(std::string_view(padded), h256::FromHex);
}

Address addressFromByte(uint8_t _byte)
{
    Address address;
    address[19] = _byte;
    return address;
}

// The two receipts of geth's TestMessages ReceiptsPacket69 vector.
std::vector<protocol::EthReceiptData> gethVectorReceipts()
{
    protocol::EthReceiptData first;
    first.type = 0;
    first.status = 0;  // ReceiptStatusFailed
    first.cumulativeGasUsed = 333;
    first.logs.push_back({addressFromByte(0x11),
        {hashFromShortHex("dead"), hashFromShortHex("beef")}, fromHex("0100ff")});
    protocol::EthReceiptData second;
    second.type = 1;
    second.status = 1;  // ReceiptStatusSuccessful
    second.cumulativeGasUsed = 444;
    second.logs.push_back({addressFromByte(0x22),
        {hashFromShortHex("05668"), hashFromShortHex("9773")}, fromHex("020f0f0f0608")});
    return {first, second};
}

std::string const c_gethReceipts69 =
    "f8da820457f8d5f8d3f866808082014df85ff85d940000000000000000000000000000000000000011f842a0"
    "000000000000000000000000000000000000000000000000000000000000deada00000000000000000000000"
    "00000000000000000000000000000000000000beef830100fff86901018201bcf862f8609400000000000000"
    "00000000000000000000000022f842a000000000000000000000000000000000000000000000000000000000"
    "00005668a0000000000000000000000000000000000000000000000000000000000000977386020f0f0f0608";
}  // namespace

// geth protocol_test.go TestEmptyMessages: GetReceiptsPacket69{1111, nil} and
// ReceiptsPacket69{1111, []} both encode to c4820457c0.
BOOST_AUTO_TEST_CASE(emptyMessagesMatchGeth)
{
    eth::GetReceiptsMessage request;
    request.requestId = 1111;
    BOOST_CHECK_EQUAL(toHex(eth::encodeGetReceipts(request, 69)), "c4820457c0");
    BOOST_CHECK_EQUAL(toHex(eth::encodeGetReceipts(request, 68)), "c4820457c0");
    eth::ReceiptsMessage response;
    response.requestId = 1111;
    BOOST_CHECK_EQUAL(toHex(eth::encodeReceipts(response, 69)), "c4820457c0");
    BOOST_CHECK_EQUAL(toHex(eth::encodeReceipts(response, 68)), "c4820457c0");
    // eth/70 inserts the EIP-7975 fields: firstBlockReceiptIndex / lastBlockIncomplete.
    BOOST_CHECK_EQUAL(toHex(eth::encodeGetReceipts(request, 70)), "c582045780c0");
    BOOST_CHECK_EQUAL(toHex(eth::encodeReceipts(response, 70)), "c582045780c0");
}

// geth TestMessages: GetReceiptsPacket69{1111, [deadc0de, feedbeef]}.
BOOST_AUTO_TEST_CASE(getReceiptsGolden69)
{
    eth::GetReceiptsMessage request;
    request.requestId = 1111;
    request.hashes = {hashFromShortHex("deadc0de"), hashFromShortHex("feedbeef")};
    std::string const expected =
        "f847820457f842a000000000000000000000000000000000000000000000000000000000deadc0dea0000000"
        "00000000000000000000000000000000000000000000000000feedbeef";
    BOOST_CHECK_EQUAL(toHex(eth::encodeGetReceipts(request, 69)), expected);
    auto decoded = eth::decodeGetReceipts(hexRef(expected), 69);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->requestId, 1111u);
    BOOST_CHECK_EQUAL(decoded->firstBlockReceiptIndex, 0u);
    BOOST_REQUIRE_EQUAL(decoded->hashes.size(), 2u);
    BOOST_CHECK(decoded->hashes[1] == hashFromShortHex("feedbeef"));
    // The same bytes read as eth/70 misalign: the hash list is not an integer.
    BOOST_CHECK(!eth::decodeGetReceipts(hexRef(expected), 70).has_value());
}

BOOST_AUTO_TEST_CASE(getReceipts70RoundTripCarriesFirstIndex)
{
    eth::GetReceiptsMessage request;
    request.requestId = 7;
    request.firstBlockReceiptIndex = 300;
    request.hashes = {hashFromShortHex("01")};
    auto encoded = eth::encodeGetReceipts(request, 70);
    // [07, 82012c, [a0..01]]
    BOOST_CHECK_EQUAL(toHex(encoded).substr(0, 10), "e60782012c");
    auto decoded = eth::decodeGetReceipts(ref(encoded), 70);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->firstBlockReceiptIndex, 300u);
    BOOST_REQUIRE_EQUAL(decoded->hashes.size(), 1u);
    BOOST_CHECK(decoded->hashes[0] == request.hashes[0]);
}

// geth TestMessages: ReceiptsPacket69{1111, [[receipt0, receipt1]]} — the EIP-7642
// 4-item receipts ([txType, status, cumulativeGas, logs], no bloom).
BOOST_AUTO_TEST_CASE(receiptsGolden69)
{
    eth::ReceiptsMessage response;
    response.requestId = 1111;
    response.receipts.emplace_back();
    for (auto const& receipt : gethVectorReceipts())
    {
        response.receipts[0].push_back(eth::encodeReceiptElement(receipt, 69));
    }
    BOOST_CHECK_EQUAL(toHex(eth::encodeReceipts(response, 69)), c_gethReceipts69);

    auto decoded = eth::decodeReceipts(hexRef(c_gethReceipts69), 69);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->requestId, 1111u);
    BOOST_CHECK(!decoded->lastBlockIncomplete);
    BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded->receipts[0].size(), 2u);
    auto const expected = gethVectorReceipts();
    for (size_t i = 0; i < 2; ++i)
    {
        auto element = eth::decodeReceiptElement(ref(decoded->receipts[0][i]), 69);
        BOOST_REQUIRE(element.has_value());
        BOOST_CHECK(*element == expected[i]);  // bloom zero on both sides
    }
}

// eth/70 wraps the same per-block lists with the lastBlockIncomplete flag (0x80 / 0x01).
BOOST_AUTO_TEST_CASE(receipts70FlagRoundTrip)
{
    eth::ReceiptsMessage response;
    response.requestId = 1111;
    response.receipts.emplace_back();
    for (auto const& receipt : gethVectorReceipts())
    {
        response.receipts[0].push_back(eth::encodeReceiptElement(receipt, 70));
    }
    response.lastBlockIncomplete = true;
    auto encoded = eth::encodeReceipts(response, 70);
    // Same payload as the eth/69 vector plus the flag byte after the request id.
    BOOST_CHECK_EQUAL(toHex(encoded).substr(0, 12), "f8db82045701");
    BOOST_CHECK_EQUAL(toHex(encoded).substr(12), c_gethReceipts69.substr(10));
    auto decoded = eth::decodeReceipts(ref(encoded), 70);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK(decoded->lastBlockIncomplete);
    BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
    BOOST_CHECK_EQUAL(decoded->receipts[0].size(), 2u);

    response.lastBlockIncomplete = false;
    encoded = eth::encodeReceipts(response, 70);
    BOOST_CHECK_EQUAL(toHex(encoded).substr(0, 12), "f8db82045780");
    decoded = eth::decodeReceipts(ref(encoded), 70);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK(!decoded->lastBlockIncomplete);
    // A flag outside {0,1} is malformed.
    auto bad = fromHex("f8db82045702" + c_gethReceipts69.substr(10));
    BOOST_CHECK(!eth::decodeReceipts(ref(bad), 70).has_value());
}

// eth/68 carries the receipts-trie form: legacy receipts as bare lists, typed receipts as
// RLP strings of 0xNN || rlp([status, cumulativeGas, bloom, logs]).
BOOST_AUTO_TEST_CASE(receipts68TrieFormWithBloom)
{
    auto receipts = gethVectorReceipts();
    receipts[1].logsBloom[0] = 0xab;  // typed receipt with a non-empty bloom
    eth::ReceiptsMessage response;
    response.requestId = 1;
    response.receipts.emplace_back();
    for (auto const& receipt : receipts)
    {
        response.receipts[0].push_back(eth::encodeReceiptElement(receipt, 68));
    }
    // Legacy receipt: a list whose third item is the 256-byte bloom (b9 0100 ...).
    auto const& legacy = response.receipts[0][0];
    BOOST_CHECK(legacy[0] >= 0xc0);
    BOOST_CHECK_NE(toHex(legacy).find("b90100"), std::string::npos);
    // Typed receipt: an RLP string (b9 ...) whose content starts with the type byte 0x01.
    auto const& typed = response.receipts[0][1];
    BOOST_CHECK_EQUAL(typed[0], 0xb9);
    BOOST_CHECK_EQUAL(typed[3], 0x01);
    bcos::bytes trieForm;
    protocol::EthReceipt(receipts[1]).rlpEncode(trieForm);
    BOOST_CHECK(bcos::bytes(typed.begin() + 3, typed.end()) == trieForm);

    auto encoded = eth::encodeReceipts(response, 68);
    auto decoded = eth::decodeReceipts(ref(encoded), 68);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded->receipts[0].size(), 2u);
    for (size_t i = 0; i < 2; ++i)
    {
        auto element = eth::decodeReceiptElement(ref(decoded->receipts[0][i]), 68);
        BOOST_REQUIRE(element.has_value());
        BOOST_CHECK(*element == receipts[i]);  // bloom round-trips on eth/68
    }
    // The eth/68 bytes read as eth/69 are not 4-item receipts: the decode fails or
    // produces elements that decodeReceiptElement rejects.
    auto as69 = eth::decodeReceipts(ref(encoded), 69);
    if (as69.has_value())
    {
        BOOST_CHECK(!eth::decodeReceiptElement(ref(as69->receipts[0][0]), 69).has_value());
    }
}

// decodeReceipts keeps every eth/68 element as the OPAQUE wire item (ReceiptsMessage): a
// typed receipt stays the RLP string (b9 .. 01 ..), so re-encoding the decoded message —
// encodeReceipts splices elements verbatim — reproduces the original bytes. Unwrapping
// the string here (takeTx) made the re-encoding emit a bare 0x01 || list instead.
BOOST_AUTO_TEST_CASE(receipts68DecodeKeepsTheWireWrapper)
{
    auto receipts = gethVectorReceipts();
    receipts[1].logsBloom[0] = 0xab;
    eth::ReceiptsMessage response;
    response.requestId = 1;
    response.receipts.emplace_back();
    for (auto const& receipt : receipts)
    {
        response.receipts[0].push_back(eth::encodeReceiptElement(receipt, 68));
    }
    auto encoded = eth::encodeReceipts(response, 68);
    auto decoded = eth::decodeReceipts(ref(encoded), 68);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_REQUIRE_EQUAL(decoded->receipts.size(), 1u);
    BOOST_REQUIRE_EQUAL(decoded->receipts[0].size(), 2u);
    auto const& typed = decoded->receipts[0][1];
    BOOST_CHECK_EQUAL(typed[0], 0xb9);  // the string prefix, not the bare type byte 0x01
    BOOST_CHECK(typed == response.receipts[0][1]);
    BOOST_CHECK(decoded->receipts[0][0] == response.receipts[0][0]);
    BOOST_CHECK(eth::encodeReceipts(*decoded, 68) == encoded);
}

// Pre-Byzantium legacy receipts carry a 32-byte post-state root instead of a status.
BOOST_AUTO_TEST_CASE(postStateReceiptElementRoundTrips)
{
    protocol::EthReceiptData receipt;
    receipt.type = 0;
    receipt.postState = hashFromShortHex("1234");
    receipt.cumulativeGasUsed = 21000;
    for (uint8_t version : {68, 69, 70})
    {
        auto element = eth::encodeReceiptElement(receipt, version);
        auto decoded = eth::decodeReceiptElement(ref(element), version);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK(*decoded == receipt);
    }
}

BOOST_AUTO_TEST_CASE(malformedReceiptElementsAreRejected)
{
    // eth/69 status byte other than 0x01 is invalid (EIP-658).
    BOOST_CHECK(!eth::decodeReceiptElement(hexRef("c480028080"), 69).has_value());
    // eth/69 tx type >= 0x80 cannot be an EIP-2718 type.
    BOOST_CHECK(!eth::decodeReceiptElement(hexRef("c58180018080"), 69).has_value());
    // trailing items after logs
    BOOST_CHECK(!eth::decodeReceiptElement(hexRef("c58001808001"), 69).has_value());
    // a string where a receipt list is expected
    BOOST_CHECK(!eth::decodeReceiptElement(hexRef("80"), 69).has_value());
    // not a list at all
    BOOST_CHECK(!eth::decodeReceipts(hexRef("80"), 69).has_value());
    BOOST_CHECK(!eth::decodeGetReceipts(hexRef("80"), 69).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
