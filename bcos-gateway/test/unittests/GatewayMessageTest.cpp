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

#define BOOST_TEST_MAIN

#include <bcos-gateway/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PMessageV2.h>
#include <bcos-gateway/libp2p/Service.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
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
    auto r = encodeMsg->encode(*buffer.get());

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
    BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), 0);

    auto decodeMsg1 = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    // decode with less length

    if (_version > 0)
    {
        BOOST_CHECK_THROW(decodeMsg1->decode(bytesConstRef(buffer->data(), buffer->size() - 1)),
            std::out_of_range);
    }
    else
    {
        auto ret1 = decodeMsg1->decode(bytesConstRef(buffer->data(), buffer->size() - 1));
        BOOST_CHECK_EQUAL(ret1, MessageDecodeStatus::MESSAGE_INCOMPLETE);
    }

    {
        auto factory = std::make_shared<P2PMessageFactory>();
        // default P2PMessage object
        auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
        encodeMsg->setVersion(_version);
        encodeMsg->setPacketType(GatewayMessageType::PeerToPeerMessage);

        auto buffer = std::make_shared<bytes>();
        auto r = encodeMsg->encode(*buffer.get());
        BOOST_CHECK_EQUAL(r, false);
    }
    // test invalid message
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = asBytes(invalidMessage);
    auto p2pMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    p2pMsg->setVersion(_version);
    if (_version > 0)
    {
        BOOST_CHECK_THROW(p2pMsg->decode(ref(invalidMsgBytes)), std::out_of_range);
    }
    else
    {
        BOOST_CHECK_EQUAL(
            p2pMsg->decode(ref(invalidMsgBytes)), MessageDecodeStatus::MESSAGE_INCOMPLETE);
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
    uint16_t ext = 0x1111;
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
    encodeMsg->setPayload(payload);

    auto buffer = std::make_shared<bytes>();
    auto r = encodeMsg->encode(*buffer.get());
    BOOST_CHECK_EQUAL(r, true);

    BOOST_CHECK_EQUAL(buffer->size(), headerLen + payload->size());

    // decode default
    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK_EQUAL(ret, headerLen + payload->size());
    BOOST_CHECK_EQUAL(decodeMsg->length(), headerLen + payload->size());
    BOOST_CHECK_EQUAL(decodeMsg->packetType(), packetType);
    BOOST_CHECK_EQUAL(decodeMsg->seq(), seq);
    BOOST_CHECK_EQUAL(decodeMsg->ext(), ext);
    BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), payload->size());

    // test invalid message
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = asBytes(invalidMessage);
    auto p2pMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    p2pMsg->setVersion(_version);
    if (_version > 0)
    {
        BOOST_CHECK_THROW(p2pMsg->decode(ref(invalidMsgBytes)), std::out_of_range);
    }
    else
    {
        BOOST_CHECK_EQUAL(
            p2pMsg->decode(ref(invalidMsgBytes)), MessageDecodeStatus::MESSAGE_INCOMPLETE);
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
        auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
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
        auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
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

        auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
        auto dstNodeIDPtr = std::make_shared<bytes>(dstNodeID.begin(), dstNodeID.end());

        options->setGroupID(groupID);
        options->setSrcNodeID(srcNodeIDPtr);
        options->dstNodeIDs().push_back(dstNodeIDPtr);

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
        auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
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
            std::string(decodeOptions->srcNodeID()->begin(), decodeOptions->srcNodeID()->end()));
        BOOST_CHECK_EQUAL(0, decodeOptions->dstNodeIDs().size());
    }

    {
        auto options = std::make_shared<P2PMessageOptions>();
        std::string groupID = "group";
        std::string srcNodeID = "nodeID";
        std::string dstNodeID = "nodeID";
        uint16_t moduleID = 11;

        auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
        auto dstNodeIDPtr = std::make_shared<bytes>(dstNodeID.begin(), dstNodeID.end());

        options->setModuleID(moduleID);
        options->setGroupID(groupID);
        options->setSrcNodeID(srcNodeIDPtr);
        auto& dstNodeIDS = options->dstNodeIDs();
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
            std::string(decodeOptions->srcNodeID()->begin(), decodeOptions->srcNodeID()->end()));
        BOOST_CHECK_EQUAL(3, decodeOptions->dstNodeIDs().size());
        for (size_t i = 0; i < 3; ++i)
        {
            BOOST_CHECK_EQUAL(dstNodeID, std::string(decodeOptions->dstNodeIDs()[i]->begin(),
                                             decodeOptions->dstNodeIDs()[i]->end()));
        }
    }
}

void testP2PMessageCodec(std::shared_ptr<MessageFactory> factory, uint32_t _version = 0)
{
    auto encodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    encodeMsg->setVersion(_version);

    uint16_t version = 0x1234;
    uint32_t seq = 0x12345678;
    uint16_t packetType = GatewayMessageType::PeerToPeerMessage;
    uint16_t ext = 0x1111;
    auto payload = std::make_shared<bytes>(10000, 'a');

    encodeMsg->setVersion(version);
    encodeMsg->setSeq(seq);
    encodeMsg->setPacketType(packetType);
    encodeMsg->setExt(ext);
    encodeMsg->setPayload(payload);

    auto options = std::make_shared<P2PMessageOptions>();
    std::string groupID = "group";
    std::string srcNodeID = "nodeID";
    std::string dstNodeID = "nodeID";

    auto srcNodeIDPtr = std::make_shared<bytes>(srcNodeID.begin(), srcNodeID.end());
    auto dstNodeIDPtr = std::make_shared<bytes>(dstNodeID.begin(), dstNodeID.end());

    options->setGroupID(groupID);
    options->setSrcNodeID(srcNodeIDPtr);
    auto& dstNodeIDS = options->dstNodeIDs();
    dstNodeIDS.push_back(dstNodeIDPtr);
    dstNodeIDS.push_back(dstNodeIDPtr);

    encodeMsg->setOptions(options);

    auto buffer = std::make_shared<bytes>();
    auto r = encodeMsg->encode(*buffer.get());
    BOOST_CHECK(r);

    auto decodeMsg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    auto ret = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK(ret > 0);

    BOOST_CHECK_EQUAL(decodeMsg->version(), version);
    BOOST_CHECK_EQUAL(decodeMsg->packetType(), packetType);
    BOOST_CHECK_EQUAL(decodeMsg->seq(), seq);
    BOOST_CHECK_EQUAL(decodeMsg->ext(), ext);
    BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), payload->size());

    auto decodeOptions = decodeMsg->options();
    BOOST_CHECK_EQUAL(groupID, decodeOptions->groupID());
    BOOST_CHECK_EQUAL(srcNodeID,
        std::string(decodeOptions->srcNodeID()->begin(), decodeOptions->srcNodeID()->end()));
    BOOST_CHECK_EQUAL(2, decodeOptions->dstNodeIDs().size());
    for (size_t i = 0; i < 2; ++i)
    {
        BOOST_CHECK_EQUAL(dstNodeID, std::string(decodeOptions->dstNodeIDs()[i]->begin(),
                                         decodeOptions->dstNodeIDs()[i]->end()));
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

BOOST_AUTO_TEST_SUITE_END()
