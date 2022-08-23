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
 * @brief test for ServiceClient
 * @file ServiceClientTest.h
 * @author: yujiechen
 * @date 2021-10-13
 */
#include "bcos-tars-protocol/client/GatewayServiceClient.h"
#include "bcos-tars-protocol/client/LedgerServiceClient.h"
#include "bcos-tars-protocol/client/PBFTServiceClient.h"
#include "bcos-tars-protocol/client/RpcServiceClient.h"
#include "bcos-tars-protocol/client/SchedulerServiceClient.h"
#include "bcos-tars-protocol/client/TxPoolServiceClient.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace bcos;
using namespace bcos::test;
namespace bcostars
{
namespace test
{

#if 0  

BOOST_FIXTURE_TEST_SUITE(testServiceClient, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testGatewayService)
{
    bcostars::GatewayServicePrx proxy;
    std::make_shared<GatewayServiceClient>(proxy, "", nullptr);
}

BOOST_AUTO_TEST_CASE(testPBFTService)
{
    bcostars::PBFTServicePrx proxy;
    std::make_shared<PBFTServiceClient>(proxy);
}

BOOST_AUTO_TEST_CASE(testRpcService)
{
    bcostars::RpcServicePrx proxy;
    std::make_shared<RpcServiceClient>(proxy, "");
}

BOOST_AUTO_TEST_CASE(testTxPoolService)
{
    bcostars::TxPoolServicePrx proxy;
    std::make_shared<TxPoolServiceClient>(proxy, nullptr, nullptr);
}
BOOST_AUTO_TEST_CASE(testLedgerService)
{
    bcostars::LedgerServicePrx prx;
    std::make_shared<LedgerServiceClient>(prx, nullptr);
}
BOOST_AUTO_TEST_CASE(testSchedulerService)
{
    bcostars::SchedulerServicePrx prx;
    std::make_shared<SchedulerServiceClient>(prx, nullptr);
}

BOOST_AUTO_TEST_CASE(testEndPointToString)
{
    {
        std::string serviceName = "HelloApp.HelloServant.HelloObj";
        std::string host = "127.0.0.1";
        uint16_t port = 1111;

        auto r = endPointToString(serviceName, host, port);
        BOOST_CHECK_EQUAL(r, "HelloApp.HelloServant.HelloObj@tcp -h 127.0.0.1 -p 1111");
    }

    {
        std::string serviceName = "HelloApp.HelloServant.HelloObj";
        std::string host = "127.0.0.1";
        uint16_t port = 12345;

        tars::TC_Endpoint endPoint(host, port, 0);

        auto r = endPointToString(serviceName, host, port);
        BOOST_CHECK_EQUAL(r, "HelloApp.HelloServant.HelloObj@tcp -h 127.0.0.1 -p 12345");
    }

    {
        std::string serviceName = "HelloApp.HelloServant.HelloObj";
        std::vector<tars::TC_Endpoint> endPoints;
        BOOST_CHECK_THROW(endPointToString(serviceName, endPoints), bcos::InvalidParameter);
    }

    {
        std::string serviceName = "HelloApp.HelloServant.HelloObj";

        std::string host0 = "127.0.0.1";
        uint16_t port0 = 11111;
        tars::TC_Endpoint endPoint0(host0, port0, 0);

        std::string host1 = "127.0.0.2";
        uint16_t port1 = 22222;
        tars::TC_Endpoint endPoint1(host1, port1, 0);

        std::string host2 = "127.0.0.3";
        uint16_t port2 = 33333;
        tars::TC_Endpoint endPoint2(host2, port2, 0);

        std::vector<tars::TC_Endpoint> endPoints;
        endPoints.push_back(endPoint0);
        endPoints.push_back(endPoint1);
        endPoints.push_back(endPoint2);

        auto r = endPointToString(serviceName, endPoints);
        BOOST_CHECK_EQUAL(
            "HelloApp.HelloServant.HelloObj@tcp -h 127.0.0.1 -p 11111:tcp -h 127.0.0.2 -p "
            "22222:tcp -h 127.0.0.3 -p 33333",
            r);
    }
}

BOOST_AUTO_TEST_SUITE_END()

#endif
}  // namespace test
}  // namespace bcostars