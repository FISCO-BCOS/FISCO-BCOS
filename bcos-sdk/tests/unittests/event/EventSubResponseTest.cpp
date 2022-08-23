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
#include <bcos-cpp-sdk/event/EventSubResponse.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(EventSubResponseTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_EventSubResponse)
{
    {
        std::string id = "0x12345";
        int status = 111;
        auto resp = std::make_shared<bcos::cppsdk::event::EventSubResponse>();
        resp->setId(id);
        resp->setStatus(status);
        auto json = resp->generateJson();

        resp->setStatus(0);
        resp->setId("");

        auto r = resp->fromJson(json);

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(id, resp->id());
        BOOST_CHECK_EQUAL(status, resp->status());

        BOOST_CHECK(!resp->fromJson("{}"));
        BOOST_CHECK(!resp->fromJson("aaa"));
    }
}

BOOST_AUTO_TEST_CASE(test_EventSubResponse_Event)
{
    {
        auto resp = std::make_shared<bcos::cppsdk::event::EventSubResponse>();
        auto json =
            "{\"id\":\"0x123\",\"status\":0,\"result\":{\"blockNumber\":111,\"events\":[]}}";
        auto r = resp->fromJson(json);
        BOOST_CHECK(r);

        BOOST_CHECK_EQUAL("0x123", resp->id());
        BOOST_CHECK_EQUAL(0, resp->status());

        BOOST_CHECK(resp->jResp().isMember("result"));
        BOOST_CHECK(resp->jResp()["result"].isMember("blockNumber"));
        BOOST_CHECK_EQUAL(resp->jResp()["result"]["blockNumber"].asInt64(), 111);
    }
}

BOOST_AUTO_TEST_SUITE_END()