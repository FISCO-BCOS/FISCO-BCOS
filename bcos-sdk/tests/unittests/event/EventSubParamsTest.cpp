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
 * @brief test for EventSubParams
 * @file EventSubParamsTest.cpp
 * @author: octopus
 * @date 2021-09-22
 */
#include <bcos-cpp-sdk/event/EventSubParams.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(EventSubParamsTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_EventSubParams)
{
    {
        auto params = std::make_shared<bcos::cppsdk::event::EventSubParams>();
        BOOST_CHECK(params->fromBlock() < 0);
        BOOST_CHECK(params->toBlock() < 0);
        BOOST_CHECK(params->addresses().empty());
        BOOST_CHECK(params->topics().empty());
    }

    {
        int64_t fromBlk = 123;
        int64_t toBlk = 456;
        std::string addr = "0x123456";
        std::string topic = "0x45678";

        auto params = std::make_shared<bcos::cppsdk::event::EventSubParams>();
        params->setFromBlock(fromBlk);
        params->setToBlock(toBlk);
        params->addAddress(addr);


        BOOST_CHECK_EQUAL(params->fromBlock(), fromBlk);
        BOOST_CHECK_EQUAL(params->toBlock(), toBlk);
        BOOST_CHECK_EQUAL(params->addresses().size(), 1);

        auto r = params->addTopic(0, topic);
        BOOST_CHECK(r);
        r = params->addTopic(1, topic);
        BOOST_CHECK(r);
        r = params->addTopic(2, topic);
        BOOST_CHECK(r);
        r = params->addTopic(3, topic);
        BOOST_CHECK(r);
        r = params->addTopic(4, topic);
        BOOST_CHECK(!r);

        BOOST_CHECK_EQUAL(params->topics().size(), 5);
        for (auto& topics : params->topics())
        {
            BOOST_CHECK_EQUAL(topics.size(), 1);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()