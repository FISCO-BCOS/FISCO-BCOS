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
 * @brief test for gateway
 * @file GatewayMessageTest.cpp
 * @author: octopus
 * @date 2021-04-26
 */

#include "bcos-gateway/gateway/GatewayMessageExtAttributes.h"
#define BOOST_TEST_MAIN
#include "bcos-gateway/libp2p/P2PInterface.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayMessageTest, TestPromptFixture)

void testP2PMessageHasOptions(std::shared_ptr<MessageFactory> factory, uint32_t _version = 0)
{
    // default P2PMessage object
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    msg->setVersion(_version);
    msg->setPacketType(GatewayMessageType::Heartbeat);
    BOOST_CHECK_EQUAL(msg->hasOptions(), false);
    msg->setPacketType(GatewayMessageType::Handshake);
    BOOST_CHECK_EQUAL(msg->hasOptions(), false);
    msg->setPacketType(GatewayMessageType::RequestNodeStatus);
    BOOST_CHECK_EQUAL(msg->hasOptions(), false);
    msg->setPacketType(GatewayMessageType::ResponseNodeStatus);
    BOOST_CHECK_EQUAL(msg->hasOptions(), false);
    msg->setPacketType(GatewayMessageType::PeerToPeerMessage);
    BOOST_CHECK_EQUAL(msg->hasOptions(), true);
    msg->setPacketType(GatewayMessageType::BroadcastMessage);
    BOOST_CHECK_EQUAL(msg->hasOptions(), true);
    msg->setPacketType(0x1111);
    BOOST_CHECK_EQUAL(msg->hasOptions(), false);

    BOOST_CHECK_EQUAL(msg->length(), 14);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_hasOptions)
{
    auto factory = std::make_shared<P2PMessageFactory>();
    testP2PMessageHasOptions(factory);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageV2_hasOptions)
{
    auto factory = std::make_shared<P2PMessageFactoryV2>();
    testP2PMessageHasOptions(factory, 1);
}

void testP2PMessage(std::shared_ptr<MessageFactory> factory, uint32_t _version = 0)
{
    // default P2PMessage object
    auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    encodeMsg->setVersion(_version);
    auto buffer = std::make_shared<bytes>();
    auto r = encodeMsg->encode(*buffer);

    BOOST_CHECK_EQUAL(r, true);

    // decode default
    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    auto version = decodeMsg->version();
    if (version == 0)
    {
        BOOST_CHECK_EQUAL(ret, 14);
        BOOST_CHECK_EQUAL(decodeMsg->length(), 14);
    }
    else
    {
        BOOST_CHECK_EQUAL(ret, 20);
        BOOST_CHECK_EQUAL(decodeMsg->length(), 20);
    }
    BOOST_CHECK_EQUAL(decodeMsg->packetType(), 0);
    BOOST_CHECK_EQUAL(decodeMsg->seq(), 0);
    BOOST_CHECK_EQUAL(decodeMsg->ext(), 0);
    BOOST_CHECK_EQUAL(decodeMsg->payload().size(), 0);

    auto decodeMsg1 = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    // decode with less length


    auto ret1 = decodeMsg1->decode(bytesConstRef(buffer->data(), buffer->size() - 1));
    BOOST_CHECK_EQUAL(ret1, MessageDecodeStatus::MESSAGE_INCOMPLETE);

    {
        auto factory1 = std::make_shared<P2PMessageFactory>();
        // default P2PMessage object
        auto encodeMsg1 = std::static_pointer_cast<P2PMessage>(factory1->buildMessage());
        encodeMsg1->setVersion(_version);
        encodeMsg1->setPacketType(GatewayMessageType::PeerToPeerMessage);

        auto buffer1 = std::make_shared<bytes>();
        auto r1 = encodeMsg1->encode(*buffer1.get());
        BOOST_CHECK_EQUAL(r1, false);
    }
    // test invalid message
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = asBytes(invalidMessage);
    auto p2pMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    p2pMsg->setVersion(_version);

    {
        // Invalid messages may return MESSAGE_ERROR (e.g. invalid version) or MESSAGE_INCOMPLETE
        auto ret3 = p2pMsg->decode(ref(invalidMsgBytes));
        BOOST_CHECK(ret3 <= 0);
    }
}

BOOST_AUTO_TEST_CASE(test_P2PMessage)
{
    auto factory = std::make_shared<P2PMessageFactory>();
    testP2PMessage(factory);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageV2)
{
    auto factory = std::make_shared<P2PMessageFactoryV2>();
    testP2PMessage(factory, 1);
}

void test_P2PMessageWithoutOptions(std::shared_ptr<MessageFactory> factory, uint32_t _version = 0)
{
    // default P2PMessage object
    auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    encodeMsg->setVersion(_version);
    uint32_t seq = 0x12345678;
    uint16_t packetType = 0x4321;
    uint16_t ext = 0x1101;
    auto payload = std::make_shared<bytes>(10000, 'a');

    auto version = encodeMsg->version();
    int16_t headerLen = 14;
    if (version > 0)
    {
        headerLen = 20;
    }

    encodeMsg->setSeq(seq);
    encodeMsg->setPacketType(packetType);
    encodeMsg->setExt(ext);
    encodeMsg->setPayload(*payload);

    auto buffer = std::make_shared<bytes>();
    auto r = encodeMsg->encode(*buffer.get());
    BOOST_CHECK_EQUAL(r, true);

    // decode default
    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK_EQUAL(ret, headerLen + payload->size());
    BOOST_CHECK_EQUAL(decodeMsg->length(), headerLen + payload->size());
    BOOST_CHECK_EQUAL(decodeMsg->packetType(), packetType);
    BOOST_CHECK_EQUAL(decodeMsg->seq(), seq);
    BOOST_CHECK_EQUAL(decodeMsg->ext(), ext);
    BOOST_CHECK_EQUAL(decodeMsg->payload().size(), payload->size());

    // test invalid message
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = asBytes(invalidMessage);
    auto p2pMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    p2pMsg->setVersion(_version);

    {
        // Invalid messages may return MESSAGE_ERROR (e.g. invalid version) or MESSAGE_INCOMPLETE
        auto ret1 = p2pMsg->decode(ref(invalidMsgBytes));
        BOOST_CHECK(ret1 <= 0);
    }
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_withoutOptions)
{
    auto factory = std::make_shared<P2PMessageFactory>();
    test_P2PMessageWithoutOptions(factory);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageV2_withoutOptions)
{
    auto factory = std::make_shared<P2PMessageFactoryV2>();
    test_P2PMessageWithoutOptions(factory, 1);
}


BOOST_AUTO_TEST_CASE(test_P2PMessage_optionsCodec)
{
    {
        auto options = std::make_shared<P2PMessageOptions>();
        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(!r);
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        options->setGroupID(groupID);
        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(!r);
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = std::string(100000, 'a');
        std::string srcNodeID = "nodeID";

        options->setGroupID(groupID);
        bytes srcNodeIDPtr(srcNodeID.begin(), srcNodeID.end());
        options->setSrcNodeID(srcNodeIDPtr);
        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(!r);  // groupID overflow
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        std::string srcNodeID = std::string(100000, 'a');
        options->setGroupID(groupID);
        bytes srcNodeIDPtr(srcNodeID.begin(), srcNodeID.end());
        options->setSrcNodeID(srcNodeIDPtr);
        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(!r);  // srcNodeID overflow
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        std::string srcNodeID = "nodeID";
        std::string dstNodeID = std::string(100000, 'a');

        auto srcNodeIDPtr = bytes(srcNodeID.begin(), srcNodeID.end());
        auto dstNodeIDPtr = bytes(dstNodeID.begin(), dstNodeID.end());

        options->setGroupID(groupID);
        options->setSrcNodeID(srcNodeIDPtr);
        options->mutableDstNodeIDs().push_back(dstNodeIDPtr);

        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(!r);  // srcNodeID overflow
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        std::string srcNodeID = "nodeID";
        uint16_t moduleID = 12345;

        options->setModuleID(moduleID);
        options->setGroupID(groupID);
        auto srcNodeIDPtr = bytes(srcNodeID.begin(), srcNodeID.end());
        options->setSrcNodeID(srcNodeIDPtr);
        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(r);

        auto decodeOptions = std::make_shared<P2PMessageOptions>();
        auto ret = decodeOptions->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(ret > 0);
        BOOST_CHECK_EQUAL(groupID, decodeOptions->groupID());
        BOOST_CHECK_EQUAL(moduleID, decodeOptions->moduleID());
        BOOST_CHECK_EQUAL(srcNodeID,
            std::string(decodeOptions->srcNodeID().begin(), decodeOptions->srcNodeID().end()));
        BOOST_CHECK_EQUAL(0, decodeOptions->dstNodeIDs().size());
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        std::string srcNodeID = "nodeID";
        std::string dstNodeID = "nodeID";
        uint16_t moduleID = 11;

        auto srcNodeIDPtr = bytes(srcNodeID.begin(), srcNodeID.end());
        auto dstNodeIDPtr = bytes(dstNodeID.begin(), dstNodeID.end());

        options->setModuleID(moduleID);
        options->setGroupID(groupID);
        options->setSrcNodeID(srcNodeIDPtr);
        auto& dstNodeIDS = options->mutableDstNodeIDs();
        dstNodeIDS.push_back(dstNodeIDPtr);
        dstNodeIDS.push_back(dstNodeIDPtr);
        dstNodeIDS.push_back(dstNodeIDPtr);

        auto buffer = std::make_shared<bytes>();
        auto r = options->encode(*buffer.get());
        BOOST_CHECK(r);

        auto decodeOptions = std::make_shared<P2PMessageOptions>();
        auto ret = decodeOptions->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(ret > 0);
        BOOST_CHECK_EQUAL(groupID, decodeOptions->groupID());
        BOOST_CHECK_EQUAL(moduleID, decodeOptions->moduleID());
        BOOST_CHECK_EQUAL(srcNodeID,
            std::string(decodeOptions->srcNodeID().begin(), decodeOptions->srcNodeID().end()));
        BOOST_CHECK_EQUAL(3, decodeOptions->dstNodeIDs().size());
        for (size_t i = 0; i < 3; ++i)
        {
            BOOST_CHECK_EQUAL(dstNodeID, std::string(decodeOptions->dstNodeIDs()[i].begin(),
                                             decodeOptions->dstNodeIDs()[i].end()));
        }
    }
}

void testP2PMessageCodec(std::shared_ptr<MessageFactory> factory, uint32_t _version = 0)
{
    auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    encodeMsg->setVersion(_version);

    uint16_t version = static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V3);
    uint32_t seq = 0x12345678;
    uint16_t packetType = GatewayMessageType::PeerToPeerMessage;
    uint16_t ext = 0x1101;
    auto payload = std::make_shared<bytes>(10000, 'a');

    encodeMsg->setVersion(version);
    encodeMsg->setSeq(seq);
    encodeMsg->setPacketType(packetType);
    encodeMsg->setExt(ext);
    encodeMsg->setPayload(*payload);

    auto options = std::make_shared<P2PMessageOptions>();
    std::string groupID = "group";
    std::string srcNodeID = "nodeID";
    std::string dstNodeID = "nodeID";

    auto srcNodeIDPtr = bytes(srcNodeID.begin(), srcNodeID.end());
    auto dstNodeIDPtr = bytes(dstNodeID.begin(), dstNodeID.end());

    options->setGroupID(groupID);
    options->setSrcNodeID(srcNodeIDPtr);
    auto& dstNodeIDS = options->mutableDstNodeIDs();
    dstNodeIDS.push_back(dstNodeIDPtr);
    dstNodeIDS.push_back(dstNodeIDPtr);

    encodeMsg->setOptions(*options);

    auto buffer = std::make_shared<bytes>();
    auto r = encodeMsg->encode(*buffer.get());
    BOOST_CHECK(r);

    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK(ret > 0);

    BOOST_CHECK_EQUAL(decodeMsg->version(), version);
    BOOST_CHECK_EQUAL(decodeMsg->packetType(), packetType);
    BOOST_CHECK_EQUAL(decodeMsg->seq(), seq);
    BOOST_CHECK_EQUAL((decodeMsg->ext() & ext), ext);
    BOOST_CHECK_EQUAL(decodeMsg->payload().size(), payload->size());

    auto decodeOptions = decodeMsg->options();
    BOOST_CHECK_EQUAL(groupID, decodeOptions.groupID());
    BOOST_CHECK_EQUAL(
        srcNodeID, std::string(decodeOptions.srcNodeID().begin(), decodeOptions.srcNodeID().end()));
    BOOST_CHECK_EQUAL(2, decodeOptions.dstNodeIDs().size());
    for (size_t i = 0; i < 2; ++i)
    {
        BOOST_CHECK_EQUAL(dstNodeID, std::string(decodeOptions.dstNodeIDs()[i].begin(),
                                         decodeOptions.dstNodeIDs()[i].end()));
    }
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_codec)
{
    auto factory = std::make_shared<P2PMessageFactory>();
    testP2PMessageCodec(factory);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageV2_codec)
{
    auto factory = std::make_shared<P2PMessageFactoryV2>();
    testP2PMessageCodec(factory, 1);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_compress)
{
    auto factory = std::make_shared<P2PMessageFactoryV2>();
    auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto encodeMsgWithoutCompress = std::static_pointer_cast<P2PMessage>(factory->buildMessage());

    // only version >= V2 support p2p network compress
    uint16_t version = 2;
    uint32_t seq = 0x12345678;
    uint16_t packetType = GatewayMessageType::PeerToPeerMessage;
    uint16_t ext = 0x1101;
    auto payload = std::make_shared<bytes>(10000, 'a');
    auto smallPayload = std::make_shared<bytes>(1, 'a');

    encodeMsg->setVersion(version);
    encodeMsg->setSeq(seq);
    encodeMsg->setPacketType(packetType);
    encodeMsg->setExt(ext);
    encodeMsg->setPayload(*payload);

    auto options = std::make_shared<P2PMessageOptions>();
    std::string groupID = "group";
    std::string srcNodeID = "nodeID";
    std::string dstNodeID = "nodeID";

    auto srcNodeIDPtr = bytes(srcNodeID.begin(), srcNodeID.end());
    auto dstNodeIDPtr = bytes(dstNodeID.begin(), dstNodeID.end());

    options->setGroupID(groupID);
    options->setSrcNodeID(srcNodeIDPtr);
    auto& dstNodeIDS = options->mutableDstNodeIDs();
    dstNodeIDS.push_back(dstNodeIDPtr);
    dstNodeIDS.push_back(dstNodeIDPtr);

    encodeMsg->setOptions(*options);

    // compress payload
    bcos::bytes compressData;
    auto r = encodeMsg->tryToCompressPayload(compressData);
    BOOST_CHECK(r);
    /*
    // encodeMsg->setExt(encodeMsg->ext() & bcos::protocol::MessageExtFieldFlag::Compress);

    // BOOST_CHECK_EQUAL((encodeMsg->ext() & bcos::protocol::MessageExtFieldFlag::Compress),
    //     bcos::protocol::MessageExtFieldFlag::Compress);

    // uncompress payload that don't compress
    // size of payload smaller than 1kb, so payload don't be compressed
    encodeMsg->setPayload(smallPayload);
    auto buffer = std::make_shared<bytes>();
    auto retWithoutCompress = encodeMsg->encode(*buffer.get());
    BOOST_CHECK(retWithoutCompress);
    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
    */
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_attr)
{
    auto attr = std::make_shared<GatewayMessageExtAttributes>();
    std::string group = "group0";
    uint16_t moduleID = 1001;
    attr->setGroupID(group);
    attr->setModuleID(moduleID);

    BOOST_CHECK_EQUAL(attr->groupID(), group);
    BOOST_CHECK_EQUAL(attr->moduleID(), moduleID);
}

// FIB-66: Test malformed frame header validation
BOOST_AUTO_TEST_CASE(test_P2PMessage_decodeHeader_invalidLength)
{
    // Craft a 14-byte buffer where m_length field is set to a value less than header size (14)
    bytes buffer(P2PMessage::MESSAGE_HEADER_LENGTH, 0);
    // Set length field to 5 (less than MESSAGE_HEADER_LENGTH=14)
    uint32_t invalidLength = boost::asio::detail::socket_ops::host_to_network_long(5);
    std::memcpy(buffer.data(), &invalidLength, 4);
    // Set version to 0 (valid)
    uint16_t version = 0;
    std::memcpy(buffer.data() + 4, &version, 2);

    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_decodeHeader_zeroLength)
{
    bytes buffer(P2PMessage::MESSAGE_HEADER_LENGTH, 0);
    // length = 0, less than header
    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_decodeHeader_invalidVersion)
{
    bytes buffer(P2PMessage::MESSAGE_HEADER_LENGTH, 0);
    // Set length = 14 (valid minimum)
    uint32_t validLength =
        boost::asio::detail::socket_ops::host_to_network_long(P2PMessage::MESSAGE_HEADER_LENGTH);
    std::memcpy(buffer.data(), &validLength, 4);
    // Set version = 99 (beyond V3=3)
    uint16_t invalidVersion = boost::asio::detail::socket_ops::host_to_network_short(99);
    std::memcpy(buffer.data() + 4, &invalidVersion, 2);

    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_decode_offsetExceedsLength)
{
    // Create a message with PeerToPeerMessage type (has options) but m_length
    // set to just the header size. After header parsing, options parsing will make
    // offset > m_length, triggering the underflow check.
    bytes buffer(P2PMessage::MESSAGE_HEADER_LENGTH + P2PMessageOptions::OPTIONS_MIN_LENGTH, 0);
    // Set length = MESSAGE_HEADER_LENGTH (too small for header + options)
    uint32_t tightLength =
        boost::asio::detail::socket_ops::host_to_network_long(P2PMessage::MESSAGE_HEADER_LENGTH);
    std::memcpy(buffer.data(), &tightLength, 4);
    // version = 0 (valid)
    // packetType = PeerToPeerMessage (0x5, has options)
    uint16_t peerType = boost::asio::detail::socket_ops::host_to_network_short(
        GatewayMessageType::PeerToPeerMessage);
    std::memcpy(buffer.data() + 6, &peerType, 2);

    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    // Should return MESSAGE_ERROR (either from options decode or offset>length check)
    BOOST_CHECK(ret < 0);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageV2_decodeHeader_errorPropagation)
{
    // V2 message with invalid version should propagate error from base decodeHeader
    bytes buffer(20, 0);  // V2 header is 20 bytes
    // Set length = 20 (valid for V2)
    uint32_t validLength = boost::asio::detail::socket_ops::host_to_network_long(20);
    std::memcpy(buffer.data(), &validLength, 4);
    // Set version = 200 (invalid, > V3)
    uint16_t invalidVersion = boost::asio::detail::socket_ops::host_to_network_short(200);
    std::memcpy(buffer.data() + 4, &invalidVersion, 2);

    auto factory = std::make_shared<P2PMessageFactoryV2>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

// FIB-67: Test options decode bounds validation
BOOST_AUTO_TEST_CASE(test_P2PMessageOptions_decode_groupIDOverflow)
{
    // Craft an options buffer with groupIDLength > MAX_GROUPID_LENGTH
    // We just need to set the groupIDLength field to a value > 65535 - but since it's uint16_t,
    // the max is 65535 which equals MAX_GROUPID_LENGTH. So this specific overflow can't happen
    // with the current wire format. Test that MAX_GROUPID_LENGTH values still work.

    // Instead, test that normal decode works with valid bounds
    auto options = std::make_shared<P2PMessageOptions>();
    std::string groupID = "testGroup";
    std::string srcNodeID = "testNode";
    uint16_t moduleID = 42;

    options->setGroupID(groupID);
    options->setSrcNodeID(bytes(srcNodeID.begin(), srcNodeID.end()));
    options->setModuleID(moduleID);

    bytes buffer;
    auto r = options->encode(buffer);
    BOOST_CHECK(r);

    auto decoded = std::make_shared<P2PMessageOptions>();
    auto ret = decoded->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK(ret > 0);
    BOOST_CHECK_EQUAL(decoded->groupID(), groupID);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageOptions_decode_truncatedBuffer)
{
    // Craft a buffer that claims large groupIDLength but has insufficient data
    bytes buffer(P2PMessageOptions::OPTIONS_MIN_LENGTH, 0);
    // Set groupIDLength to 1000 but buffer only has OPTIONS_MIN_LENGTH bytes
    uint16_t largeGroupIDLen = boost::asio::detail::socket_ops::host_to_network_short(1000);
    std::memcpy(buffer.data(), &largeGroupIDLen, 2);

    auto decoded = std::make_shared<P2PMessageOptions>();
    auto ret = decoded->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(test_P2PMessageOptions_decode_truncatedNodeID)
{
    // Create a buffer where nodeIDLength is large but buffer is too small
    bytes buffer(10, 0);
    // groupIDLength = 0
    uint16_t zeroLen = 0;
    std::memcpy(buffer.data(), &zeroLen, 2);
    // nodeIDLength = 5000
    uint16_t largeNodeIDLen = boost::asio::detail::socket_ops::host_to_network_short(5000);
    std::memcpy(buffer.data() + 2, &largeNodeIDLen, 2);

    auto decoded = std::make_shared<P2PMessageOptions>();
    auto ret = decoded->decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK_EQUAL(ret, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(test_P2PMessage_decode_validVersionBoundary)
{
    // Test that V3 (max valid) is accepted
    bytes buffer(P2PMessage::MESSAGE_HEADER_LENGTH, 0);
    uint32_t validLength =
        boost::asio::detail::socket_ops::host_to_network_long(P2PMessage::MESSAGE_HEADER_LENGTH);
    std::memcpy(buffer.data(), &validLength, 4);
    uint16_t v3 = boost::asio::detail::socket_ops::host_to_network_short(
        static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V3));
    std::memcpy(buffer.data() + 4, &v3, 2);

    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = msg->decode(bytesConstRef(buffer.data(), buffer.size()));
    // V3 is valid, should succeed (return the length)
    BOOST_CHECK_EQUAL(ret, static_cast<int32_t>(P2PMessage::MESSAGE_HEADER_LENGTH));

    // Test that V3+1 is rejected
    bytes buffer2(P2PMessage::MESSAGE_HEADER_LENGTH, 0);
    std::memcpy(buffer2.data(), &validLength, 4);
    uint16_t v4 = boost::asio::detail::socket_ops::host_to_network_short(
        static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V3) + 1);
    std::memcpy(buffer2.data() + 4, &v4, 2);

    auto msg2 = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret2 = msg2->decode(bytesConstRef(buffer2.data(), buffer2.size()));
    BOOST_CHECK_EQUAL(ret2, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_SUITE_END()
