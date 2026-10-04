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
static_assert(bcos::network::FrameDecoder<P2PDecoder>);

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
    BOOST_CHECK(empty.status == bcos::network::FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(empty.declaredLength, Message::MESSAGE_HEADER_LENGTH);

    // fewer bytes than the fixed header: the decoder asks for the header length
    bytes partial(10, 0xff);
    auto meta = decoder.tryDecode(ref(partial));
    BOOST_CHECK(meta.status == bcos::network::FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(meta.declaredLength, Message::MESSAGE_HEADER_LENGTH);

    // header complete but the frame body missing: asks for the declared frame length
    auto frame = buildFrame(0, 0x11223344, 0, bytes(100, 'x'));
    bytes headOnly(frame.begin(), frame.begin() + Message::MESSAGE_HEADER_LENGTH);
    meta = decoder.tryDecode(ref(headOnly));
    BOOST_CHECK(meta.status == bcos::network::FrameMeta::Status::NeedMoreData);
    BOOST_CHECK_EQUAL(meta.declaredLength, frame.size());
}

BOOST_AUTO_TEST_CASE(decodesCompleteV0Frame)
{
    P2PDecoder decoder;
    bytes payload = {1, 2, 3, 4, 5};
    auto frame = buildFrame(0, 0x11223344, 0, payload);

    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(meta.consumed, frame.size());
    BOOST_CHECK(!meta.takeBuffer);
    BOOST_CHECK_EQUAL(meta.frame.size(), frame.size());
    auto frameData = meta.frameData();
    BOOST_CHECK_EQUAL(frameData.size(), frame.size());
    BOOST_CHECK(std::equal(frameData.begin(), frameData.end(), frame.begin()));

    // the decoded frame round-trips through Message::decode
    Message message;
    BOOST_REQUIRE(message.decode(meta.frameData()) > 0);
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
    BOOST_REQUIRE(metaA.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(metaA.consumed, frameA.size());

    bytesConstRef rest(stream.data() + metaA.consumed, stream.size() - metaA.consumed);
    auto metaB = decoder.tryDecode(rest);
    BOOST_REQUIRE(metaB.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(metaB.consumed, frameB.size());
}

BOOST_AUTO_TEST_CASE(protocolErrorOnBadLength)
{
    P2PDecoder decoder;

    // length smaller than the fixed header
    auto frame = buildFrame(0, 0, 0, {});
    Message::stampLength(frame, Message::MESSAGE_HEADER_LENGTH - 1);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == bcos::network::FrameMeta::Status::ProtocolError);

    // length beyond the gateway maximum
    Message::stampLength(frame, static_cast<uint32_t>(MAX_MESSAGE_LENGTH) + 1);
    meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == bcos::network::FrameMeta::Status::ProtocolError);
}

BOOST_AUTO_TEST_CASE(protocolErrorOnUnsupportedVersion)
{
    P2PDecoder decoder;
    auto frame = buildFrame(0, 0, 0, {});
    stamp16(frame, 4, 0xFFFF);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_CHECK(meta.status == bcos::network::FrameMeta::Status::ProtocolError);
}

BOOST_AUTO_TEST_CASE(decodesV2ExtendedHeader)
{
    P2PDecoder decoder;
    auto frame = buildFrame((uint16_t)bcos::protocol::ProtocolVersion::V2, 7,
        (uint16_t)bcos::protocol::MessageExtFieldFlag::RESPONSE, bytes(8, 'p'), "srcNode",
        "dstNode");

    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK_EQUAL(meta.consumed, frame.size());

    // the response-decision fields (resp flag, seq, dst) are peeked at the libp2p boundary,
    // not lifted by the decoder
    auto respInfo = Message::peekResponseFrameInfo(meta.frameData());
    BOOST_REQUIRE(respInfo.has_value());
    BOOST_CHECK(respInfo->isResp);
    BOOST_CHECK_EQUAL(respInfo->seq, 7);
    BOOST_CHECK_EQUAL(respInfo->dstP2PNodeID, "dstNode");
}

BOOST_AUTO_TEST_CASE(truncatedExtendedHeaderRejectedAtLibp2pBoundary)
{
    P2PDecoder decoder;
    // version V2 promises the extended header (ttl/src/dst), but the declared frame length
    // only covers the 14-byte base header. Stream splitting no longer inspects the extended
    // header, so the decoder hands the frame over; the libp2p boundary rejects it instead.
    auto frame = buildFrame(0, 0, 0, {});
    stamp16(frame, 4, (uint16_t)bcos::protocol::ProtocolVersion::V2);
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == bcos::network::FrameMeta::Status::Frame);

    auto respInfo = Message::peekResponseFrameInfo(meta.frameData());
    BOOST_REQUIRE(respInfo.has_value());
    BOOST_CHECK(!respInfo->isResp);

    // a RESPONSE-flagged variant drives the peek down the extended-header walk: bounds
    // violation yields nullopt, and Message::decode rejects the frame as well
    stamp16(frame, 12, (uint16_t)bcos::protocol::MessageExtFieldFlag::RESPONSE);
    BOOST_CHECK(!Message::peekResponseFrameInfo(ref(frame)).has_value());

    Message message;
    BOOST_CHECK_THROW(message.decode(ref(frame)), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(peekResponseFrameInfoBoundsAndVariants)
{
    // too short for the fixed header
    bytes tiny(10, 0xff);
    BOOST_CHECK(!Message::peekResponseFrameInfo(ref(tiny)).has_value());

    // V0 response frame: isResp set, no extended header on the wire -> empty dstP2PNodeID
    auto v0Resp = buildFrame(0, 1, (uint16_t)bcos::protocol::MessageExtFieldFlag::RESPONSE, {});
    auto v0Info = Message::peekResponseFrameInfo(ref(v0Resp));
    BOOST_REQUIRE(v0Info.has_value());
    BOOST_CHECK(v0Info->isResp);
    BOOST_CHECK(v0Info->dstP2PNodeID.empty());

    // non-response V2 frame: peek stops at the ext flag and never walks the extended header
    auto v2Plain = buildFrame((uint16_t)bcos::protocol::ProtocolVersion::V2, 2, 0, bytes(4, 'q'),
        "srcNode", "dstNode");
    auto v2Info = Message::peekResponseFrameInfo(ref(v2Plain));
    BOOST_REQUIRE(v2Info.has_value());
    BOOST_CHECK(!v2Info->isResp);
    BOOST_CHECK(v2Info->dstP2PNodeID.empty());
}

BOOST_AUTO_TEST_CASE(largeFrameRequestsTakeBuffer)
{
    P2PDecoder decoder;
    // at/above FRAME_TAKE_BUFFER_THRESHOLD the decoder leaves `frame` empty and asks the read
    // loop to swap the whole receive buffer in instead of copying
    auto frame = buildFrame(0, 9, 0, bytes(bcos::network::FRAME_TAKE_BUFFER_THRESHOLD, 'L'));
    auto meta = decoder.tryDecode(ref(frame));
    BOOST_REQUIRE(meta.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK(meta.takeBuffer);
    BOOST_CHECK(meta.frame.empty());
    BOOST_CHECK_EQUAL(meta.consumed, frame.size());

    // just below the threshold: the copy path, frameData() exposes exactly the frame
    auto smallFrame = buildFrame(
        0, 10, 0, bytes(bcos::network::FRAME_TAKE_BUFFER_THRESHOLD - Message::MESSAGE_HEADER_LENGTH - 1, 's'));
    auto smallMeta = decoder.tryDecode(ref(smallFrame));
    BOOST_REQUIRE(smallMeta.status == bcos::network::FrameMeta::Status::Frame);
    BOOST_CHECK(!smallMeta.takeBuffer);
    BOOST_CHECK_EQUAL(smallMeta.frame.size(), smallFrame.size());
    BOOST_CHECK_EQUAL(smallMeta.frameOffset, 0);
    BOOST_REQUIRE_EQUAL(smallMeta.frameData().size(), smallFrame.size());
    BOOST_CHECK(
        std::equal(smallMeta.frameData().begin(), smallMeta.frameData().end(), smallFrame.begin()));
}

BOOST_AUTO_TEST_SUITE_END()
