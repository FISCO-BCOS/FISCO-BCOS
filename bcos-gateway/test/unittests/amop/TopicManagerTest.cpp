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
 * @date 2021-06-21
 */
#include "bcos-gateway/libamop/AirTopicManager.h"
#include <bcos-gateway/libamop/TopicManager.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::amop;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(TopicManagerTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_initTopicManager)
{
    auto topicManager = std::make_shared<LocalTopicManager>("", nullptr);
    {
        auto jsonValue = topicManager->queryTopicsSubByClient();

        uint32_t topicSeq;
        TopicItems topicItems;
        auto b = topicManager->parseTopicItemsJson(topicSeq, topicItems, jsonValue);
        BOOST_TEST(b);
        BOOST_TEST(topicSeq == 1);
        BOOST_TEST(topicItems.empty());
    }

    {
        auto seq = topicManager->topicSeq();
        BOOST_TEST(seq == 1);
        topicManager->incTopicSeq();
        BOOST_TEST(topicManager->topicSeq() == (seq + 1));
        topicManager->incTopicSeq();
        BOOST_TEST(topicManager->topicSeq() == (seq + 2));
    }
}

BOOST_AUTO_TEST_CASE(test_parseTopicItemsJson)
{
    auto topicManager = std::make_shared<LocalTopicManager>("", nullptr);
    {
        uint32_t topicSeq;
        TopicItems topicItems;
        std::string json = "";
        auto r = topicManager->parseTopicItemsJson(topicSeq, topicItems, json);
        BOOST_TEST(!r);
    }
    {
        uint32_t topicSeq;
        TopicItems topicItems;
        std::string json = "{\"topicSeq\":111,\"topicItems\":[]}";
        auto r = topicManager->parseTopicItemsJson(topicSeq, topicItems, json);
        BOOST_TEST(r);
        BOOST_TEST(topicSeq == 111);
        BOOST_TEST(topicItems.size() == 0);
    }
    {
        uint32_t topicSeq;
        TopicItems topicItems;
        std::string json = "{\"topicSeq\":123,\"topicItems\":[\"a\",\"b\",\"c\"]}";
        auto r = topicManager->parseTopicItemsJson(topicSeq, topicItems, json);
        BOOST_TEST(r);
        BOOST_TEST(topicSeq == 123);
        BOOST_TEST(topicItems.size() == 3);
        auto a = std::string("a");
        BOOST_TEST(std::find_if(topicItems.begin(), topicItems.end(),
                        [a](const TopicItem& _topicItem) { return _topicItem.topicName() == a; }) !=
                    topicItems.end());
        auto b = std::string("b");
        BOOST_TEST(std::find_if(topicItems.begin(), topicItems.end(),
                        [b](const TopicItem& _topicItem) { return _topicItem.topicName() == b; }) !=
                    topicItems.end());
        auto c = std::string("c");
        BOOST_TEST(std::find_if(topicItems.begin(), topicItems.end(),
                        [c](const TopicItem& _topicItem) { return _topicItem.topicName() == c; }) !=
                    topicItems.end());
    }
}

BOOST_AUTO_TEST_CASE(test_subTopics)
{
    auto topicManager = std::make_shared<LocalTopicManager>("", nullptr);

    std::string clientID = "client";
    {
        TopicItems topicItems;
        auto r = topicManager->queryTopicItemsByClient(clientID, topicItems);
        BOOST_TEST(!r);
        BOOST_TEST(topicItems.empty());
    }


    {
        TopicItems topicItems;
        std::vector<std::string> topics{"topic0", "topic1", "topic2", "topic3"};
        for (const auto& topic : topics)
        {
            topicItems.insert(TopicItem(topic));
        }
        auto seq = topicManager->topicSeq();
        // sub topics
        topicManager->subTopic(clientID, topicItems);
        topicItems.clear();
        auto r = topicManager->queryTopicItemsByClient(clientID, topicItems);
        BOOST_TEST(r);
        BOOST_TEST(topicItems.size() == topics.size());
        BOOST_TEST(topicManager->topicSeq() == (seq + 1));

        auto jsonValue = topicManager->queryTopicsSubByClient();
        BOOST_TEST(!jsonValue.empty());

        uint32_t topicSeqFromJson;
        TopicItems topicItemsFromJson;
        auto b = topicManager->parseTopicItemsJson(topicSeqFromJson, topicItemsFromJson, jsonValue);
        BOOST_TEST(b);
        BOOST_TEST(topicSeqFromJson = topicManager->topicSeq());
        BOOST_TEST(topicItemsFromJson.size() == topicItems.size());
    }
}

BOOST_AUTO_TEST_SUITE_END()