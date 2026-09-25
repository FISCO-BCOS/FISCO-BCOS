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
 * @brief Unit tests for P2PDecoder — the FrameDecoder that splits the gateway P2P byte stream
 *        into frames for the generic libnetwork session machinery.
 * @file P2PDecoderTest.cpp
 * @date 2026-09-21
 */

#include "bcos-gateway/Common.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(P2PDecoderTest, TestPromptFixture)

namespace
{
// The decoder must satisfy the libnetwork concept it is plugged into.
static_assert(FrameDecoder<P2PDecoder>);

void stamp16(bytes& _buf, std::size_t _offset, uint16_t _value)
{
    uint16_t network = boost::asio::detail::socket_ops::host_to_network_short(_value);
    std::memcpy(_buf.data() + _offset, &network, sizeof(network));
}

// Build a complete wire frame for the given version; src/dst only land on the wire for
// version > V0 (the extended header).
bytes buildFrame(uint16_t _version, uint32_t _seq, uint16_t _ext, bytes const& _payload,
    std::string const& _src = {}, std::string const& _dst = {})
{
    Message message;
    message.setVersion(_version);
    message.setSeq(_seq);
    message.setExt(_ext);
    message.setSrcP2PNodeID(_src);
    message.setDstP2PNodeID(_dst);
    bytes header;
    BOOST_REQUIRE(message.encodeHeader(header));
    bytes frame = std::move(header);
    frame.insert(frame.end(), _payload.begin(), _payload.end());
    Message::stampLength(frame, static_cast<uint32_t>(frame.size()));
    return frame;
}
}  // namespace

BOOST_AUTO_TEST_CASE(needMoreDataOnShortBuffer)
{
    P2PDecoder decoder;

    auto empty = decoder.tryDecode(bytesConstRef{});
    BOOST_CHECK(empty.status == FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(empty.declaredLength, Message::MESSAGE_HEADER_LENGTH);

    // fewer bytes than the fixed header: the decoder asks for the header length
    bytes partial(10, 0xff);
    auto meta = decoder.tryDecode(ref(partial));
    BOOST_CHECK(meta.status == FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(meta.declaredLength, Message::MESSAGE_HEADER_LENGTH);

    // header complete but the frame body missing: asks for the declared frame length
    auto frame = buildFrame(0, 0x11223344, 0, bytes(100, 'x'));
    bytes headOnly(frame.begin(), frame.begin() + Message::MESSAGE_HEADER_LENGTH);
    meta = decoder.tryDecode(ref(headOnly));
    BOOST_CHECK(meta.status == FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(meta.declaredLength, frame.size());
}

BOOST_AUTO_TEST_CASE(decodesCompleteV0Frame)
{
    P2PDecoder decoder;
    bytes payload = {1, 2, 3, 4, 5};
    auto frame = buildFrame(0, 0x11223344, 0, payload);

    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(meta.consumed, frame.size());
    BOOST_CHECK_EQUAL(meta.seq, 0x11223344);
    BOOST_CHECK(!meta.isResp);
    BOOST_CHECK(meta.dstID.empty());
    BOOST_CHECK_EQUAL(meta.frame.size(), frame.size());
    BOOST_CHECK(std::equal(meta.frame.begin(), meta.frame.end(), frame.begin()));

    // the decoded frame round-trips through Message::decode
    Message message;
    BOOST_REQUIRE(message.decode(ref(meta.frame)) > 0);
    BOOST_CHECK_EQUAL(message.seq(), 0x11223344);
    BOOST_CHECK_EQUAL(message.payload().size(), payload.size());
}

BOOST_AUTO_TEST_CASE(decodesStickyFramesBackToBack)
{
    P2PDecoder decoder;
    auto frameA = buildFrame(0, 1, 0, bytes(10, 'a'));
    auto frameB = buildFrame(0, 2, 0, bytes(20, 'b'));

    bytes stream = frameA;
    stream.insert(stream.end(), frameB.begin(), frameB.end());

    auto metaA = decoder.tryDecode(ref(stream));
    BOOST_REQUIRE(metaA.status == FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(metaA.consumed, frameA.size());
    BOOST_CHECK_EQUAL(metaA.seq, 1);

    bytesConstRef rest(stream.data() + metaA.consumed, stream.size() - metaA.consumed);
    auto metaB = decoder.tryDecode(rest);
    BOOST_REQUIRE(metaB.status == FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(metaB.consumed, frameB.size());
    BOOST_CHECK_EQUAL(metaB.seq, 2);
}

BOOST_AUTO_TEST_CASE(protocolErrorOnBadLength)
{
    P2PDecoder decoder;

    // length smaller than the fixed header
    auto frame = buildFrame(0, 0, 0, {});
    Message::stampLength(frame, Message::MESSAGE_HEADER_LENGTH - 1);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == FrameMeta::Status::ProtocolError);

    // length beyond the gateway maximum
    Message::stampLength(frame, static_cast<uint32_t>(MAX_MESSAGE_LENGTH) + 1);
    meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == FrameMeta::Status::ProtocolError);
}

BOOST_AUTO_TEST_CASE(protocolErrorOnUnsupportedVersion)
{
    P2PDecoder decoder;
    auto frame = buildFrame(0, 0, 0, {});
    stamp16(frame, 4, 0xFFFF);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == FrameMeta::Status::ProtocolError);
}

BOOST_AUTO_TEST_CASE(decodesV2ExtendedHeader)
{
    P2PDecoder decoder;
    auto frame = buildFrame((uint16_t)bcos::protocol::ProtocolVersion::V2, 7,
        (uint16_t)bcos::protocol::MessageExtFieldFlag::RESPONSE, bytes(8, 'p'), "srcNode",
        "dstNode");

    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(meta.consumed, frame.size());
    BOOST_CHECK_EQUAL(meta.seq, 7);
    BOOST_CHECK(meta.isResp);
    BOOST_CHECK_EQUAL(meta.dstID, "dstNode");
}

BOOST_AUTO_TEST_CASE(protocolErrorOnTruncatedExtendedHeader)
{
    P2PDecoder decoder;
    // version V2 promises the extended header (ttl/src/dst), but the declared frame length
    // only covers the 14-byte base header
    auto frame = buildFrame(0, 0, 0, {});
    stamp16(frame, 4, (uint16_t)bcos::protocol::ProtocolVersion::V2);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == FrameMeta::Status::ProtocolError);
}

BOOST_AUTO_TEST_SUITE_END()
