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
 * @brief test for TopicManager
 * @file TopicManagerTest.cpp
 * @author: octopus
 * @date 2021-09-22
 */
#include <bcos-cpp-sdk/amop/AMOPRequest.h>
#include <bcos-cpp-sdk/amop/TopicManager.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::cppsdk::amop;
using namespace bcos::test;
using namespace bcos::protocol;

BOOST_FIXTURE_TEST_SUITE(TopicManagerTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(test_AMOPRequestEncodeDecode)
{
    auto amopRequestFactory = std::make_shared<AMOPRequestFactory>();
    std::string dataStr = "testAMOPRequest";
    auto request = amopRequestFactory->buildRequest();
    request->setData(bcos::bytesConstRef((byte*)dataStr.data(), dataStr.size()));
    request->setVersion(10023);
    std::string topic = "testAMOPRequest+-@topic";
    request->setTopic(topic);

    BOOST_TEST(request->version() == 10023);
    BOOST_TEST(request->topic() == topic);
    BOOST_TEST(*(request->data().data()) == *(dataStr.data()));

    // encode
    bytes encodedData;
    request->encode(encodedData);

    // decode
    auto decodedRequest = amopRequestFactory->buildRequest(ref(encodedData));
    BOOST_TEST(decodedRequest->version() == request->version());
    BOOST_TEST(decodedRequest->topic() == request->topic());
    BOOST_TEST(*(decodedRequest->data().data()) == *(request->data().data()));
}
BOOST_AUTO_TEST_CASE(test_TopicManager)
{
    {
        auto topicManager = std::make_shared<TopicManager>();
        auto topics = topicManager->topics();
        BOOST_TEST(topics.size() == 0);

        std::string topic1 = "a";
        std::string topic2 = "a";
        std::string topic3 = "a";

        auto r = topicManager->addTopic(topic1);
        BOOST_TEST(r);
        r = topicManager->addTopic(topic1);
        BOOST_TEST(!r);
        r = topicManager->addTopic(topic2);
        BOOST_TEST(!r);
        r = topicManager->addTopic(topic3);
        BOOST_TEST(!r);

        topics = topicManager->topics();
        BOOST_TEST(topics.size() == 1);

        r = topicManager->removeTopic(topic1);
        BOOST_TEST(r);

        r = topicManager->removeTopic(topic1);
        BOOST_TEST(!r);

        topics = topicManager->topics();
        BOOST_TEST(topics.size() == 0);

        BOOST_TEST(!topicManager->toJson().empty());
    }

    {
        auto topicManager = std::make_shared<TopicManager>();
        BOOST_TEST(topicManager->topics().size() == 0);

        std::string topic1 = "a";
        std::string topic2 = "b";
        std::string topic3 = "c";
        std::set<std::string> topics{topic1, topic2, topic3};

        auto r = topicManager->addTopics(topics);
        BOOST_TEST(r);
        r = topicManager->addTopics(topics);
        BOOST_TEST(!r);

        BOOST_TEST(topics.size() == topics.size());

        r = topicManager->removeTopics(topics);
        BOOST_TEST(r);

        r = topicManager->removeTopics(topics);
        BOOST_TEST(!r);

        topics = topicManager->topics();
        BOOST_TEST(topics.size() == 0);
        BOOST_TEST(!topicManager->toJson().empty());
    }

    {
        auto topicManager = std::make_shared<TopicManager>();
        BOOST_TEST(topicManager->topics().size() == 0);

        std::string topic1 = "a";
        std::string topic2 = "a";
        std::string topic3 = "a";
        std::set<std::string> topics{topic1, topic2, topic3};

        auto r = topicManager->addTopics(topics);
        BOOST_TEST(r);
        r = topicManager->addTopics(topics);
        BOOST_TEST(!r);

        topics = topicManager->topics();
        BOOST_TEST(topics.size() == topics.size());

        r = topicManager->removeTopic(topic1);
        BOOST_TEST(r);

        r = topicManager->removeTopic(topic2);
        BOOST_TEST(!r);

        r = topicManager->removeTopic(topic3);
        BOOST_TEST(!r);

        r = topicManager->removeTopics(topics);
        BOOST_TEST(!r);

        topics = topicManager->topics();
        BOOST_TEST(topics.size() == 0);
        BOOST_TEST(!topicManager->toJson().empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()
