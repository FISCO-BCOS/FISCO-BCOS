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
 * @brief test for WsMessage
 * @file WsMessageTest.cpp
 * @author: octopus
 * @date 2021-07-12
 */

#include <bcos-boostssl/websocket/WsMessage.h>

#include <boost/test/unit_test.hpp>

using namespace bcos;

using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

BOOST_AUTO_TEST_SUITE(WsMessageTest)

BOOST_AUTO_TEST_CASE(test_WsMessage)
{
    auto factory = std::make_shared<WsMessageFactory>();
    auto msg = factory->buildMessage();
    auto buffer = std::make_shared<bytes>();
    auto r = msg->encode(*buffer);
    auto seq = msg->seq();

    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH);

    {
        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), 0);
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
    }
}


BOOST_AUTO_TEST_CASE(test_buildMessage)
{
    {
        int16_t status = 111;
        uint16_t type = 222;
        std::string data = "HelloWorld.";
        auto factory = std::make_shared<WsMessageFactory>();
        auto msg = factory->buildMessage();
        auto wsMessage = std::dynamic_pointer_cast<WsMessage>(msg);
        wsMessage->setStatus(status);
        wsMessage->setPacketType(type);
        wsMessage->setPayload(std::make_shared<bytes>(data.begin(), data.end()));

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        auto decodedWsMessge = std::dynamic_pointer_cast<WsMessage>(decodeMsg);
        BOOST_CHECK_EQUAL(decodedWsMessge->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload()->begin(), decodeMsg->payload()->end()));
    }

    {
        int16_t status = 222;
        uint16_t type = 111;
        std::string data = "HelloWorld.";
        auto factory = std::make_shared<WsMessageFactory>();
        auto msg = factory->buildMessage(type, std::make_shared<bytes>(data.begin(), data.end()));
        auto wsMessage = std::dynamic_pointer_cast<WsMessage>(msg);
        wsMessage->setStatus(status);
        wsMessage->setPacketType(type);

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        auto decodedWsMessge = std::dynamic_pointer_cast<WsMessage>(decodeMsg);
        BOOST_CHECK_EQUAL(decodedWsMessge->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload()->size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload()->begin(), decodeMsg->payload()->end()));
    }
    auto factory = std::make_shared<WsMessageFactory>();
    auto msg = factory->buildMessage();
    auto wsMessage = std::dynamic_pointer_cast<WsMessage>(msg);
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = bcos::bytes(invalidMessage.begin(), invalidMessage.end());
    BOOST_CHECK_THROW(wsMessage->decode(ref(invalidMsgBytes)), std::out_of_range);
}
BOOST_AUTO_TEST_SUITE_END()
