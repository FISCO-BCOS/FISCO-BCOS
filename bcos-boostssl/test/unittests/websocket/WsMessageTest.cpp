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
    auto msg = std::make_shared<WsMessage>();
    auto buffer = std::make_shared<bytes>();
    auto r = msg->encode(*buffer);
    auto seq = msg->seq();

    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH);

    {
        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), 0);
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
        auto msg = std::make_shared<WsMessage>();
        msg->setStatus(status);
        msg->setPacketType(type);
        msg->setPayload(bytes(data.begin(), data.end()));

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload().begin(), decodeMsg->payload().end()));
    }

    {
        int16_t status = 222;
        uint16_t type = 111;
        std::string data = "HelloWorld.";
        auto payload = bytes(data.begin(), data.end());
        auto msg = std::make_shared<WsMessage>();
        msg->setPacketType(type);
        msg->setPayload(std::move(payload));
        msg->setStatus(status);
        msg->setPacketType(type);

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload().begin(), decodeMsg->payload().end()));
    }
    auto msg = std::make_shared<WsMessage>();
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = bcos::bytes(invalidMessage.begin(), invalidMessage.end());
    BOOST_CHECK_THROW(msg->decode(ref(invalidMsgBytes)), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(test_newSeq)
{
    auto seq = newSeq();
    BOOST_CHECK_EQUAL(seq.size(), 32);
    for (char c : seq)
    {
        BOOST_CHECK(std::isxdigit(static_cast<unsigned char>(c)));
        BOOST_CHECK(!std::isupper(static_cast<unsigned char>(c)));
    }
    BOOST_CHECK_NE(seq, newSeq());
}
BOOST_AUTO_TEST_SUITE_END()
