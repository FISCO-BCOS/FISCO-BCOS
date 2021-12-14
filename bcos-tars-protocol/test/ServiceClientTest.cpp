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
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::test;
namespace bcostars
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(testServiceClient, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testGatewayService)
{
    bcostars::GatewayServicePrx proxy;
    std::make_shared<GatewayServiceClient>(proxy, nullptr);
}

BOOST_AUTO_TEST_CASE(testPBFTService)
{
    bcostars::PBFTServicePrx proxy;
    std::make_shared<PBFTServiceClient>(proxy);
}

BOOST_AUTO_TEST_CASE(testRpcService)
{
    bcostars::RpcServicePrx proxy;
    std::make_shared<RpcServiceClient>(proxy);
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
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcostars