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
 * @brief test for Gateway
 * @file GatewayTest.cpp
 * @author: octopus
 * @date 2021-05-21
 */

#include "../common/FrontServiceBuilder.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <range/v3/view/single.hpp>

using namespace bcos;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayTest, TestPromptFixture)

static uint nodeCount = 3;

std::vector<bcos::front::FrontService::Ptr> buildFrontServiceVector()
{
    std::string groupID = "1";
    std::string nodeIDBase = "node";
    // ../test/unittests/data
    std::string configPathBase = "./node";

    std::vector<bcos::front::FrontService::Ptr> frontServiceVector;

    for (uint i = 0; i < nodeCount; ++i)
    {
        auto frontService = buildFrontService(groupID, nodeIDBase + std::to_string(i),
            configPathBase + std::to_string(i) + "/config.ini");
        auto frontServiceWeakptr = std::weak_ptr<bcos::front::FrontService>(frontService);
        // register message dispatcher for front service
        frontService->registerModuleMessageDispatcher(
            bcos::protocol::ModuleID::AMOP, [frontServiceWeakptr](bcos::crypto::NodeIDPtr _nodeID,
                                                const std::string _id, bytesConstRef _data) {
                auto frontService = frontServiceWeakptr.lock();
                if (frontService)
                {
                    frontService->asyncSendResponse(
                        _id, bcos::protocol::ModuleID::AMOP, _nodeID, _data, [](Error::Ptr) {});
                }
            });
        frontServiceVector.push_back(frontService);
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));

    return frontServiceVector;
}

BOOST_AUTO_TEST_CASE(test_FrontServiceEcho)
{
    auto frontServiceVector = buildFrontServiceVector();
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    // echo test
    for (const auto& frontService : frontServiceVector)
    {
        frontService->asyncGetGroupNodeInfo([frontService](Error::Ptr _error,
                                                bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            BOOST_CHECK(_error == nullptr);
            auto const& nodeIDs = _groupNodeInfo->nodeIDList();
            BOOST_CHECK_EQUAL(nodeIDs.size(), nodeCount);

            for (const auto& nodeIDStr : nodeIDs)
            {
                auto nodeID = keyFactory->createKey(fromHex(nodeIDStr));
                std::string sendStr = boost::uuids::to_string(boost::uuids::random_generator()());

                auto payload = bcos::bytesConstRef((bcos::byte*)sendStr.data(), sendStr.size());

                auto result = task::syncWait(frontService->sendMessageByNodeID(
                    bcos::protocol::ModuleID::AMOP, nodeID, ::ranges::views::single(payload),
                    10000));

                BOOST_CHECK(!result.uuid.empty());
                BOOST_CHECK(result.error == nullptr);
                std::string retStr(result.payload.begin(), result.payload.end());
                BOOST_CHECK_EQUAL(sendStr, retStr);
            }
        });
    }
}

BOOST_AUTO_TEST_CASE(test_FrontServiceTimeout)
{
    auto frontServiceVector = buildFrontServiceVector();
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    // echo test
    for (const auto& frontService : frontServiceVector)
    {
        frontService->asyncGetGroupNodeInfo([frontService](Error::Ptr _error,
                                                bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            BOOST_CHECK(_error == nullptr);
            auto const& nodeIDs = _groupNodeInfo->nodeIDList();
            BOOST_CHECK_EQUAL(nodeIDs.size(), nodeCount);

            for (const auto& nodeIDStr : nodeIDs)
            {
                auto nodeID = keyFactory->createKey(fromHex(nodeIDStr));
                std::string sendStr = boost::uuids::to_string(boost::uuids::random_generator()());

                auto payload = bcos::bytesConstRef((bcos::byte*)sendStr.data(), sendStr.size());

                auto result = task::syncWait(frontService->sendMessageByNodeID(
                    bcos::protocol::ModuleID::AMOP + 1, nodeID, ::ranges::views::single(payload),
                    10000));

                BOOST_CHECK(!result.uuid.empty());
                BOOST_CHECK(result.error != nullptr);
                BOOST_CHECK_EQUAL(
                    result.error->errorCode(), bcos::protocol::CommonError::TIMEOUT);
            }
        });
    }
}

BOOST_AUTO_TEST_SUITE_END()