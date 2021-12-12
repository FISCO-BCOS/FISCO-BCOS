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
 * @brief test for front related messages
 * @file FrontMessageTest.h
 * @author: octopus
 * @date 2021-04-26
 */

#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-front/FrontMessage.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::test;
using namespace bcos::front;

BOOST_FIXTURE_TEST_SUITE(FrontMessageTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testFrontMessage_0)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    BOOST_CHECK_EQUAL(message->moduleID(), 0);
    BOOST_CHECK_EQUAL(message->ext(), 0);
    BOOST_CHECK_EQUAL(message->uuid()->size(), 0);
    BOOST_CHECK_EQUAL(message->payload().size(), 0);

    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);
    BOOST_CHECK(buffer->size() == FrontMessage::HEADER_MIN_LENGTH);
}

BOOST_AUTO_TEST_CASE(testFrontMessage_1)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    int moduleID = 0;
    int ext = 0;
    std::string uuid = "";
    std::string payload = "";

    message->setModuleID(moduleID);
    message->setExt(ext);
    message->setUuid(std::make_shared<bytes>(uuid.begin(), uuid.end()));
    auto payloadPtr = std::make_shared<bytes>(payload.begin(), payload.end());
    message->setPayload(bytesConstRef(payloadPtr->data(), payloadPtr->size()));

    // encode
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);
    BOOST_CHECK(!buffer->empty());

    // decode
    auto decodeMessage = factory->buildMessage();

    auto bcr = bytesConstRef(buffer->data(), buffer->size());

    auto r1 = decodeMessage->decode(bcr);
    BOOST_CHECK_EQUAL(r1, MessageDecodeStatus::MESSAGE_COMPLETE);

    BOOST_CHECK_EQUAL(moduleID, decodeMessage->moduleID());
    BOOST_CHECK_EQUAL(ext, decodeMessage->ext());
    BOOST_CHECK_EQUAL(
        uuid, std::string(decodeMessage->uuid()->begin(), decodeMessage->uuid()->end()));
    BOOST_CHECK_EQUAL(
        payload, std::string(decodeMessage->payload().begin(), decodeMessage->payload().end()));
}

BOOST_AUTO_TEST_CASE(testFrontMessage_2)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    int moduleID = 1;
    int ext = 2;
    std::string uuid = "1234567890";
    std::string payload = "payload";

    message->setModuleID(moduleID);
    message->setExt(ext);
    message->setUuid(std::make_shared<bytes>(uuid.begin(), uuid.end()));
    auto payloadPtr = std::make_shared<bytes>(payload.begin(), payload.end());
    message->setPayload(bytesConstRef(payloadPtr->data(), payloadPtr->size()));

    // encode
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);
    BOOST_CHECK(!buffer->empty());

    // decode
    auto decodeMessage = factory->buildMessage();

    auto bcr = bytesConstRef(buffer->data(), buffer->size());

    auto r1 = decodeMessage->decode(bcr);
    BOOST_CHECK_EQUAL(r1, MessageDecodeStatus::MESSAGE_COMPLETE);

    BOOST_CHECK_EQUAL(moduleID, decodeMessage->moduleID());
    BOOST_CHECK_EQUAL(ext, decodeMessage->ext());
    BOOST_CHECK_EQUAL(
        uuid, std::string(decodeMessage->uuid()->begin(), decodeMessage->uuid()->end()));
    BOOST_CHECK_EQUAL(
        payload, std::string(decodeMessage->payload().begin(), decodeMessage->payload().end()));
}

BOOST_AUTO_TEST_CASE(testFrontMessage_3)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    int moduleID = 1;
    int ext = 2;
    std::string uuid = "1234567890";
    std::string payload = "payload";

    message->setModuleID(moduleID);
    message->setExt(ext);
    message->setUuid(std::make_shared<bytes>(uuid.begin(), uuid.end()));
    auto payloadPtr = std::make_shared<bytes>(payload.begin(), payload.end());
    message->setPayload(bytesConstRef(payloadPtr->data(), payloadPtr->size()));

    // encode
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);
    BOOST_CHECK(!buffer->empty());

    // decode
    auto decodeMessage = factory->buildMessage();

    auto bcr = bytesConstRef(buffer->data(), buffer->size());

    auto r1 = decodeMessage->decode(bcr);
    BOOST_CHECK_EQUAL(r1, MessageDecodeStatus::MESSAGE_COMPLETE);

    BOOST_CHECK_EQUAL(moduleID, decodeMessage->moduleID());
    BOOST_CHECK_EQUAL(ext, decodeMessage->ext());
    BOOST_CHECK_EQUAL(
        uuid, std::string(decodeMessage->uuid()->begin(), decodeMessage->uuid()->end()));
    BOOST_CHECK_EQUAL(
        payload, std::string(decodeMessage->payload().begin(), decodeMessage->payload().end()));
}

BOOST_AUTO_TEST_CASE(testFrontMessage_4)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    int moduleID = 1;
    int ext = 2;
    std::string uuid =
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "567890123456789012345678901234567890123456789012345678901234567890123456"
        "78901234567890123456789012345678901234567890123456789012345678901234567"
        "8";
    std::string payload = "payload";

    message->setModuleID(moduleID);
    message->setExt(ext);
    message->setUuid(std::make_shared<bytes>(uuid.begin(), uuid.end()));
    auto payloadPtr = std::make_shared<bytes>(payload.begin(), payload.end());
    message->setPayload(bytesConstRef(payloadPtr->data(), payloadPtr->size()));

    // encode
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(!r);

    buffer->clear();
    auto decodeMessage = factory->buildMessage();
    auto r1 = decodeMessage->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK_EQUAL(r1, MessageDecodeStatus::MESSAGE_ERROR);
}

BOOST_AUTO_TEST_CASE(testFrontMessage_5)
{
    auto factory = std::make_shared<FrontMessageFactory>();
    auto message = factory->buildMessage();
    int moduleID = 111;
    std::string uuid;
    std::string payload = std::string(1000, 'x');

    message->setModuleID(moduleID);
    message->setUuid(std::make_shared<bytes>(uuid.begin(), uuid.end()));
    auto payloadPtr = std::make_shared<bytes>(payload.begin(), payload.end());
    message->setPayload(bytesConstRef(payloadPtr->data(), payloadPtr->size()));

    // encode
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    auto r = message->encode(*buffer.get());
    BOOST_CHECK(r);

    auto decodeMessage = factory->buildMessage();
    auto r1 = decodeMessage->decode(bytesConstRef(buffer->data(), buffer->size()));
    BOOST_CHECK_EQUAL(r1, MessageDecodeStatus::MESSAGE_COMPLETE);
    BOOST_CHECK_EQUAL(
        payload, std::string(decodeMessage->payload().begin(), decodeMessage->payload().end()));
}

BOOST_AUTO_TEST_SUITE_END()
