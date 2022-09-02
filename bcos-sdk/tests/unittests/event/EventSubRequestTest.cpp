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
 * @brief test for EventSubRequest
 * @file EventSubTaskStateTest.cpp
 * @author: octopus
 * @date 2021-09-22
 */
#include <bcos-cpp-sdk/event/EventSubRequest.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(EventSubRequestTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_EventSubUnsubRequestTest)
{
    {
        auto id = std::string("123");
        auto group = std::string("321");

        auto request = std::make_shared<bcos::cppsdk::event::EventSubUnsubRequest>();
        request->setId(id);
        request->setGroup(group);
        auto json = request->generateJson();

        auto decodeReq = std::make_shared<bcos::cppsdk::event::EventSubUnsubRequest>();
        auto r = decodeReq->fromJson(json);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(decodeReq->group(), group);
        BOOST_CHECK_EQUAL(decodeReq->id(), id);
    }

    {
        auto decodeReq = std::make_shared<bcos::cppsdk::event::EventSubUnsubRequest>();
        auto r = decodeReq->fromJson("{}");
        BOOST_CHECK(!r);
    }

    {
        auto decodeReq = std::make_shared<bcos::cppsdk::event::EventSubUnsubRequest>();
        auto r = decodeReq->fromJson("aaa");
        BOOST_CHECK(!r);
    }
}


BOOST_AUTO_TEST_CASE(test_EventSubSubRequestTest)
{
    {
        auto id = std::string("123");
        auto group = std::string("321");
        auto params = std::make_shared<bcos::cppsdk::event::EventSubParams>();
        auto state = std::make_shared<bcos::cppsdk::event::EventSubTaskState>();

        auto request = std::make_shared<bcos::cppsdk::event::EventSubSubRequest>();
        request->setId(id);
        request->setGroup(group);
        request->setParams(params);
        request->setState(state);

        auto json = request->generateJson();
        {
            auto request = std::make_shared<bcos::cppsdk::event::EventSubSubRequest>();
            auto r = request->fromJson(json);
            BOOST_CHECK(r);
            BOOST_CHECK_EQUAL(id, request->id());
            BOOST_CHECK_EQUAL(group, request->group());
            auto params = request->params();
            BOOST_CHECK(params->fromBlock() < 0);
            BOOST_CHECK(params->toBlock() < 0);
            BOOST_CHECK(params->addresses().empty());
            BOOST_CHECK(params->topics().empty());
        }
    }

    {
        auto id = std::string("111");
        auto group = std::string("222");
        auto fromBlk = 111;
        auto toBlk = 222;
        auto addr = std::string("0x1111");
        auto topic = std::string("topic");
        auto curBlk = 100;

        auto params = std::make_shared<bcos::cppsdk::event::EventSubParams>();
        params->setFromBlock(fromBlk);
        params->setToBlock(toBlk);
        params->addAddress(addr);
        params->addTopic(1, topic);
        params->addTopic(3, topic);

        auto state = std::make_shared<bcos::cppsdk::event::EventSubTaskState>();
        state->setCurrentBlockNumber(curBlk);

        auto request = std::make_shared<bcos::cppsdk::event::EventSubSubRequest>();
        request->setId(id);
        request->setGroup(group);
        request->setParams(params);
        request->setState(state);

        auto json = request->generateJson();
        {
            auto request = std::make_shared<bcos::cppsdk::event::EventSubSubRequest>();
            auto r = request->fromJson(json);
            BOOST_CHECK(r);
            BOOST_CHECK_EQUAL(id, request->id());
            BOOST_CHECK_EQUAL(group, request->group());
            auto params = request->params();
            BOOST_CHECK_EQUAL(params->fromBlock(), curBlk + 1);
            BOOST_CHECK_EQUAL(params->toBlock(), toBlk);
            BOOST_CHECK_EQUAL(params->addresses().size(), 1);
            BOOST_CHECK(params->addresses().find(addr) != params->addresses().end());
            BOOST_CHECK_EQUAL(params->topics().size(), 4);
            BOOST_CHECK_EQUAL(params->topics()[0].size(), 0);
            BOOST_CHECK_EQUAL(params->topics()[1].size(), 1);
            BOOST_CHECK_EQUAL(params->topics()[2].size(), 0);
            BOOST_CHECK_EQUAL(params->topics()[3].size(), 1);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()