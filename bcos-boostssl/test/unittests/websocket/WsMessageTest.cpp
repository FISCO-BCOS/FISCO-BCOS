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
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::test;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::utilities;

BOOST_FIXTURE_TEST_SUITE(WsMessageTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_WsMessage)
{
    auto factory = std::make_shared<WsMessageFactory>();
    auto msg = factory->buildMessage();
    auto buffer = std::make_shared<bytes>();
    auto r = msg->encode(*buffer);
    auto seq = std::string(msg->seq()->begin(), msg->seq()->end());

    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(msg->seq()->size(), WsMessage::SEQ_LENGTH);
    BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH);

    {
        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(buffer->data(), buffer->size());
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->data()->size(), 0);
        BOOST_CHECK_EQUAL(decodeMsg->seq()->size(), WsMessage::SEQ_LENGTH);
        auto decodeSeq = std::string(msg->seq()->begin(), msg->seq()->end());
        BOOST_CHECK_EQUAL(seq, decodeSeq);
    }
}


BOOST_AUTO_TEST_CASE(test_buildMessage)
{
    {
        uint16_t status = 111;
        uint16_t type = 222;
        std::string data = "HelloWorld.";
        auto factory = std::make_shared<WsMessageFactory>();
        auto msg = factory->buildMessage();
        msg->setStatus(status);
        msg->setType(type);
        msg->setData(std::make_shared<bytes>(data.begin(), data.end()));

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = std::string(msg->seq()->begin(), msg->seq()->end());

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(msg->seq()->size(), WsMessage::SEQ_LENGTH);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(buffer->data(), buffer->size());
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->type(), type);
        BOOST_CHECK_EQUAL(decodeMsg->data()->size(), data.size());
        BOOST_CHECK_EQUAL(decodeMsg->seq()->size(), WsMessage::SEQ_LENGTH);
        auto decodeSeq = std::string(msg->seq()->begin(), msg->seq()->end());
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(data, std::string(decodeMsg->data()->begin(), decodeMsg->data()->end()));
    }

    {
        uint16_t status = 222;
        uint16_t type = 111;
        std::string data = "HelloWorld.";
        auto factory = std::make_shared<WsMessageFactory>();
        auto msg = factory->buildMessage(type, std::make_shared<bytes>(data.begin(), data.end()));
        msg->setStatus(status);
        msg->setType(type);

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = std::string(msg->seq()->begin(), msg->seq()->end());

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(msg->seq()->size(), WsMessage::SEQ_LENGTH);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = factory->buildMessage();
        auto size = decodeMsg->decode(buffer->data(), buffer->size());
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->type(), type);
        BOOST_CHECK_EQUAL(decodeMsg->data()->size(), data.size());
        BOOST_CHECK_EQUAL(decodeMsg->seq()->size(), WsMessage::SEQ_LENGTH);
        auto decodeSeq = std::string(msg->seq()->begin(), msg->seq()->end());
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(data, std::string(decodeMsg->data()->begin(), decodeMsg->data()->end()));
    }
}
BOOST_AUTO_TEST_SUITE_END()