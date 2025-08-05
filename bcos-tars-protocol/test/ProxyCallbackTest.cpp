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
 * @brief test for TarsServantProxyCallback
 * @author: octopus
 * @date 2022-07-24
 */
#include "bcos-tars-protocol/client/GatewayServiceClient.h"
#include "bcos-tars-protocol/client/LedgerServiceClient.h"
#include "bcos-tars-protocol/client/PBFTServiceClient.h"
#include "bcos-tars-protocol/client/RpcServiceClient.h"
#include "bcos-tars-protocol/client/SchedulerServiceClient.h"
#include "bcos-tars-protocol/client/TxPoolServiceClient.h"
#include "bcos-tars-protocol/tars/GatewayService.h"
#include "bcos-utilities/Exceptions.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <servant/Communicator.h>
#include <util/tc_clientsocket.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::test;
namespace bcostars
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(TarsServantProxyCallbackTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testTarsServantProxyCallbackTest)
{
    tars::TC_Endpoint ep("127.0.0.1", 1111, 0);
    tars::TC_Endpoint ep0("127.0.0.2", 1112, 0);
    tars::TC_Endpoint ep1("127.0.0.3", 1113, 0);

    std::string serviceName = "HelloApp.HelloServer.HelloObj";

    TarsServantProxyCallback cb(serviceName, nullptr);

    int conCount = 0;
    cb.setOnConnectHandler([&conCount](const tars::TC_Endpoint& _ep) { conCount++; });

    int closeCount = 0;
    cb.setOnCloseHandler([&closeCount](const tars::TC_Endpoint& _ep) { closeCount++; });

    std::this_thread::sleep_for(std::chrono::seconds(3));

    auto activeEndpoints = cb.activeEndpoints();
    auto inactiveEndpoints = cb.inactiveEndpoints();

    BOOST_CHECK_EQUAL(cb.serviceName(), serviceName);

    BOOST_CHECK_EQUAL(activeEndpoints.size(), 0);
    BOOST_CHECK_EQUAL(inactiveEndpoints.size(), 0);
    BOOST_TEST(!cb.available());

    auto p = cb.addActiveEndpoint(ep);
    BOOST_TEST(p.first);
    BOOST_TEST(p.second == 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 1);

    p = cb.addActiveEndpoint(ep);
    BOOST_TEST(!p.first);
    BOOST_TEST(p.second == 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 1);

    p = cb.addActiveEndpoint(ep);
    BOOST_TEST(!p.first);
    BOOST_TEST(p.second == 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 1);

    p = cb.addInactiveEndpoint(ep);
    BOOST_TEST(p.first);
    BOOST_CHECK_EQUAL(p.second, 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 0);
    BOOST_CHECK_EQUAL(cb.inactiveEndpoints().size(), 1);

    p = cb.addInactiveEndpoint(ep);
    BOOST_TEST(!p.first);
    BOOST_TEST(p.second == 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 0);
    BOOST_CHECK_EQUAL(cb.inactiveEndpoints().size(), 1);

    p = cb.addInactiveEndpoint(ep);
    BOOST_TEST(!p.first);
    BOOST_TEST(p.second == 1);
    BOOST_CHECK_EQUAL(cb.activeEndpoints().size(), 0);
    BOOST_CHECK_EQUAL(cb.inactiveEndpoints().size(), 1);

    BOOST_TEST(!cb.available());

    cb.onConnect(ep);
    BOOST_TEST(cb.available());
    BOOST_CHECK_EQUAL(conCount, 1);

    cb.onConnect(ep0);
    BOOST_TEST(cb.available());
    BOOST_CHECK_EQUAL(conCount, 2);

    cb.onConnect(ep1);
    BOOST_TEST(cb.available());
    BOOST_CHECK_EQUAL(conCount, 3);

    activeEndpoints = cb.activeEndpoints();
    inactiveEndpoints = cb.inactiveEndpoints();

    BOOST_CHECK_EQUAL(activeEndpoints.size(), 3);
    BOOST_CHECK_EQUAL(inactiveEndpoints.size(), 0);

    cb.onClose(ep);
    BOOST_TEST(cb.available());
    BOOST_CHECK_EQUAL(closeCount, 1);

    cb.onClose(ep0);
    BOOST_TEST(cb.available());
    BOOST_CHECK_EQUAL(closeCount, 2);

    cb.onClose(ep1);
    BOOST_TEST(!cb.available());
    BOOST_CHECK_EQUAL(closeCount, 3);

    activeEndpoints = cb.activeEndpoints();
    inactiveEndpoints = cb.inactiveEndpoints();

    BOOST_CHECK_EQUAL(activeEndpoints.size(), 0);
    BOOST_CHECK_EQUAL(inactiveEndpoints.size(), 3);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcostars