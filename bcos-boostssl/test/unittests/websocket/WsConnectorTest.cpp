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
 * @brief test for WsConnector
 * @file WsConnectorTest.cpp
 * @author: octopus
 * @date 2021-10-04
 */

#include <bcos-boostssl/websocket/WsConnector.h>

#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;

using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

BOOST_AUTO_TEST_SUITE(WsConnectorTest)

BOOST_AUTO_TEST_CASE(test_WsConnectorTest)
{
    auto connector = std::make_shared<WsConnector>(nullptr);
    {
        std::string host = "0.0.0.0";
        uint16_t port = 1111;

        std::string endpoint = host + ":" + std::to_string(port);
        auto r = connector->insertPendingConns(endpoint);
        BOOST_CHECK(r);
        r = connector->insertPendingConns(endpoint);
        BOOST_CHECK(!r);
        r = connector->erasePendingConns(endpoint);
        BOOST_CHECK(r);
        r = connector->insertPendingConns(endpoint);
        BOOST_CHECK(r);
    }
}

BOOST_AUTO_TEST_SUITE_END()
