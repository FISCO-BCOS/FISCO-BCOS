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
 * @file EthStatus70Test.cpp
 * @brief eth/70 negotiation surface: Status keeps the EIP-7642 7-field layout (EIP-7975
 *        changes only GetReceipts/Receipts), the advertised capability ladders, and the
 *        BlockRangeUpdate (0x11) codec.
 * @date 2026/11/3
 */
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <deque>
#include <string>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(EthStatus70Test)

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
eth::StatusMessage sampleStatus(uint8_t _version)
{
    eth::StatusMessage status;
    status.protocolVersion = _version;
    status.networkId = 11155111;
    status.genesisHash =
        h256(std::string_view("0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9"),
            h256::FromHex);
    status.forkId = {0xed88b5fd, 0};
    status.eip7642 = true;
    status.earliestBlock = 0;
    status.latestBlock = 8000000;
    status.latestBlockHash =
        h256(std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
            h256::FromHex);
    return status;
}
}  // namespace

BOOST_AUTO_TEST_CASE(versionLadder)
{
    BOOST_CHECK_EQUAL(eth::kProtocolVersion, 70);
    BOOST_CHECK_EQUAL(eth::kMinProtocolVersion, 68);
    BOOST_CHECK_EQUAL(eth::kMinServingProtocolVersion, 69);
    auto outbound = eth::ethCapabilities();
    BOOST_REQUIRE_EQUAL(outbound.size(), 3u);
    BOOST_CHECK_EQUAL(outbound[0].version, 70);
    BOOST_CHECK_EQUAL(outbound[1].version, 69);
    BOOST_CHECK_EQUAL(outbound[2].version, 68);
    auto serving = eth::ethCapabilities(eth::kMinServingProtocolVersion);
    BOOST_REQUIRE_EQUAL(serving.size(), 2u);
    BOOST_CHECK_EQUAL(serving[0].version, 70);
    BOOST_CHECK_EQUAL(serving[1].version, 69);
    for (auto const& cap : outbound)
    {
        BOOST_CHECK_EQUAL(cap.name, "eth");
    }
}

// eth/70 Status = eth/69 Status: [vsn, networkid, genesis, forkid, earliest, latest, latestHash].
BOOST_AUTO_TEST_CASE(status70IsTheEip7642Layout)
{
    auto status70 = sampleStatus(70);
    auto encoded = eth::encodeStatus(status70);
    auto status69 = sampleStatus(69);
    auto encoded69 = eth::encodeStatus(status69);
    // Only the version byte (first item after the 2-byte list header) differs.
    BOOST_REQUIRE_EQUAL(encoded.size(), encoded69.size());
    BOOST_CHECK_EQUAL(encoded[2], 70);
    BOOST_CHECK_EQUAL(encoded69[2], 69);
    BOOST_CHECK(bcos::bytes(encoded.begin() + 3, encoded.end()) ==
                bcos::bytes(encoded69.begin() + 3, encoded69.end()));

    auto decoded = eth::decodeStatus(ref(encoded), 70);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK(decoded->eip7642);
    BOOST_CHECK_EQUAL(decoded->protocolVersion, 70u);
    BOOST_CHECK_EQUAL(decoded->networkId, status70.networkId);
    BOOST_CHECK(decoded->genesisHash == status70.genesisHash);
    BOOST_CHECK(decoded->forkId == status70.forkId);
    BOOST_CHECK_EQUAL(decoded->earliestBlock, 0u);
    BOOST_CHECK_EQUAL(decoded->latestBlock, 8000000u);
    BOOST_CHECK(decoded->latestBlockHash == status70.latestBlockHash);
    BOOST_CHECK(decoded->headHash == status70.latestBlockHash);
    // An eth/69 Status arriving on an eth/70-negotiated session is rejected (the embedded
    // version must match the Hello negotiation), and vice versa.
    BOOST_CHECK(!eth::decodeStatus(ref(encoded69), 70).has_value());
    BOOST_CHECK(!eth::decodeStatus(ref(encoded), 69).has_value());
    // The eth/68 6-field layout is never accepted on 70.
    auto status68 = sampleStatus(68);
    status68.eip7642 = false;
    status68.headHash = status68.latestBlockHash;
    auto const encoded68 = eth::encodeStatus(status68);
    BOOST_CHECK(!eth::decodeStatus(ref(encoded68), 70).has_value());
}

BOOST_AUTO_TEST_CASE(blockRangeUpdateCodec)
{
    eth::BlockRangeUpdateMessage update;
    update.earliestBlock = 0;
    update.latestBlock = 0x1234;
    update.latestBlockHash =
        h256(std::string_view("0x2222222222222222222222222222222222222222222222222222222222222222"),
            h256::FromHex);
    auto encoded = eth::encodeBlockRangeUpdate(update);
    // [80, 821234, a0 22*32]
    BOOST_CHECK_EQUAL(toHex(encoded),
        "e580821234a02222222222222222222222222222222222222222222222222222222222222222");
    auto decoded = eth::decodeBlockRangeUpdate(ref(encoded));
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded->earliestBlock, 0u);
    BOOST_CHECK_EQUAL(decoded->latestBlock, 0x1234u);
    BOOST_CHECK(decoded->latestBlockHash == update.latestBlockHash);
    BOOST_CHECK(decoded->valid());

    // Spec: earliest > latest must lead to a disconnect — the message decodes but is invalid.
    eth::BlockRangeUpdateMessage inverted;
    inverted.earliestBlock = 10;
    inverted.latestBlock = 5;
    auto const encodedInverted = eth::encodeBlockRangeUpdate(inverted);
    auto decodedInverted = eth::decodeBlockRangeUpdate(ref(encodedInverted));
    BOOST_REQUIRE(decodedInverted.has_value());
    BOOST_CHECK(!decodedInverted->valid());

    // Malformed: trailing item, short hash, not a list.
    BOOST_CHECK(!eth::decodeBlockRangeUpdate(hexRef("c480820102")).has_value());
    BOOST_CHECK(!eth::decodeBlockRangeUpdate(hexRef("c3808201")).has_value());
    BOOST_CHECK(!eth::decodeBlockRangeUpdate(hexRef("80")).has_value());
    BOOST_CHECK_EQUAL(eth::msg::BlockRangeUpdate, 0x11);
    BOOST_CHECK_EQUAL(eth::frameId(eth::msg::BlockRangeUpdate), 0x21u);
}

BOOST_AUTO_TEST_SUITE_END()
