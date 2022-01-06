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
 * @brief test for WsConfig
 * @file WsConfigTest.cpp
 * @author: octopus
 * @date 2021-10-04
 */

#define BOOST_TEST_MAIN

#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::test;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

BOOST_FIXTURE_TEST_SUITE(WsToolsTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_WsConfigTest)
{
    {
        auto config = std::make_shared<WsConfig>();
        config->setModel(WsModel::Mixed);
        BOOST_CHECK_EQUAL(config->listenIP(), std::string());
        BOOST_CHECK_EQUAL(config->listenPort(), 0);
        BOOST_CHECK_EQUAL(config->asClient(), true);
        BOOST_CHECK_EQUAL(config->asServer(), true);

        auto peers = std::make_shared<const std::vector<EndPoint>>();
        config->setConnectedPeers(peers);
        BOOST_CHECK_EQUAL(config->connectedPeers()->size(), 0);
    }

    {
        auto config = std::make_shared<WsConfig>();
        auto model = WsModel::Server;
        auto boostsslConfig = std::string("boostssl.ini");
        auto listenIP = std::string("127.0.0.1");
        auto listenPort = 12345;
        auto threadPoolSize = 123;

        config->setModel(model);
        config->setListenIP(listenIP);
        config->setListenPort(listenPort);
        config->setThreadPoolSize(threadPoolSize);

        BOOST_CHECK_EQUAL(config->listenIP(), listenIP);
        BOOST_CHECK_EQUAL(config->listenPort(), listenPort);
        BOOST_CHECK_EQUAL(config->asClient(), false);
        BOOST_CHECK_EQUAL(config->asServer(), true);

        auto peers = std::make_shared<const std::vector<EndPoint>>();
        config->setConnectedPeers(peers);
        BOOST_CHECK_EQUAL(config->connectedPeers()->size(), 0);
    }
}

BOOST_AUTO_TEST_CASE(test_WsToolsTest)
{
    BOOST_CHECK_EQUAL(WsTools::validIP("0.0.0.0"), true);
    BOOST_CHECK_EQUAL(WsTools::validIP("123"), false);
    BOOST_CHECK_EQUAL(WsTools::validIP("127.0.0.1"), true);
    BOOST_CHECK_EQUAL(WsTools::validIP("2001:0db8:3c4d:0015:0000:0000:1a2f:1a2b"), true);
    BOOST_CHECK_EQUAL(WsTools::validIP("0:0:0:0:0:0:0:1"), true);
    BOOST_CHECK_EQUAL(WsTools::validIP("::1"), true);

    BOOST_CHECK_EQUAL(WsTools::validPort(1111), true);
    BOOST_CHECK_EQUAL(WsTools::validPort(10), false);
    BOOST_CHECK_EQUAL(WsTools::validPort(65535), true);
}

BOOST_AUTO_TEST_SUITE_END()