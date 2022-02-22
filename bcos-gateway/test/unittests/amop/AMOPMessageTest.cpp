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
 * @brief test for AMOPMessage
 * @file AMOPMessageTest.cpp
 * @author: octopus
 * @date 2021-06-21
 */

#include <bcos-gateway/libamop/AMOPMessage.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::amop;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(AMOPMessageTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_initAMOPMessage)
{
    auto messageFactory = std::make_shared<AMOPMessageFactory>();
    {
        auto message = messageFactory->buildMessage();
        auto buffer = std::make_shared<bytes>();
        message->encode(*buffer.get());
        BOOST_CHECK_EQUAL(buffer->size(), AMOPMessage::HEADER_LENGTH);

        auto decodeMessage = messageFactory->buildMessage();
        auto r = decodeMessage->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(r > 0);
        BOOST_CHECK_EQUAL(decodeMessage->type(), 0);
        BOOST_CHECK_EQUAL(decodeMessage->data().size(), 0);
    }

    {
        uint16_t type = 111;
        std::string topic = "topic";
        std::string data = "Hello, FISCO-BCOS 3.0";
        auto message = messageFactory->buildMessage();
        message->setType(type);
        message->setData(data);
        auto buffer = std::make_shared<bytes>();
        message->encode(*buffer.get());

        auto decodeMessage = messageFactory->buildMessage();
        auto r = decodeMessage->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(r > 0);
        BOOST_CHECK_EQUAL(decodeMessage->type(), type);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMessage->data().begin(), decodeMessage->data().end()));
    }

    {
        uint16_t type = 1234;
        std::string topic(65535, '1');
        std::string data(10000, '1');
        auto message = messageFactory->buildMessage();
        message->setType(type);
        message->setData(bytesConstRef((byte*)data.data(), data.size()));
        auto buffer = std::make_shared<bytes>();
        message->encode(*buffer.get());

        auto decodeMessage = messageFactory->buildMessage();
        auto r = decodeMessage->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(r > 0);
        BOOST_CHECK_EQUAL(decodeMessage->type(), type);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMessage->data().begin(), decodeMessage->data().end()));
    }

    {
        auto decodeMessage = messageFactory->buildMessage();
        auto r = decodeMessage->decode(bytesConstRef(""));
        BOOST_CHECK(r < 0);
    }
}

BOOST_AUTO_TEST_CASE(test_AMOPMessageTopicOverflow)
{
    auto messageFactory = std::make_shared<AMOPMessageFactory>();

    uint16_t type = 1234;
    std::string data(10000, '1');
    auto message = messageFactory->buildMessage();
    message->setType(type);
    message->setData(bytesConstRef((byte*)data.data(), data.size()));
    auto buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);
}
BOOST_AUTO_TEST_SUITE_END()