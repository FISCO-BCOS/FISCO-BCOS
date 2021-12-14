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
 * @brief test for front service
 * @file FrontServiceTest.h
 * @author: octopus
 * @date 2021-04-26
 */

#define BOOST_TEST_MAIN

#include "FakeGateway.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-front/Common.h>
#include <bcos-front/FrontMessage.h>
#include <bcos-front/FrontService.h>
#include <bcos-front/FrontServiceFactory.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::test;
using namespace bcos::front;
using namespace bcos::front::test;

const static std::string g_groupID = "front.service.group";
const static std::string g_srcNodeID = "front.src.nodeid";
const static std::string g_dstNodeID_0 = "front.dst.nodeid.0";
const static std::string g_dstNodeID_1 = "front.dst.nodeid.1";

bcos::crypto::NodeIDPtr createKey(const std::string& _strNodeID)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeID = keyFactory->createKey(bytesConstRef((byte*)_strNodeID.data(), _strNodeID.size()));
    return nodeID;
}

std::shared_ptr<FrontService> buildFrontService()
{
    auto gateway = std::make_shared<FakeGateway>();
    auto srcNodeID = createKey(g_srcNodeID);

    auto threadPool = std::make_shared<ThreadPool>("frontServiceTest", 16);
    auto frontServiceFactory = std::make_shared<FrontServiceFactory>();
    frontServiceFactory->setThreadPool(threadPool);
    frontServiceFactory->setGatewayInterface(gateway);
    auto frontService = frontServiceFactory->buildFrontService(g_groupID, srcNodeID);
    frontService->start();

    gateway->setFrontService(frontService);

    return frontService;
}

BOOST_FIXTURE_TEST_SUITE(FrontServiceTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testFrontService_buildFrontService)
{
    auto frontService = buildFrontService();
    BOOST_CHECK_EQUAL(frontService->groupID(), g_groupID);
    // BOOST_CHECK_EQUAL(frontService->nodeID()->hex(), g_srcNodeID);
    BOOST_CHECK(frontService->gatewayInterface());
    BOOST_CHECK(frontService->messageFactory());
    BOOST_CHECK(frontService->ioService());
    BOOST_CHECK(frontService->callback().empty());
    BOOST_CHECK(frontService->moduleID2MessageDispatcher().empty());
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendMessageByNodeID_withoutCallback)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());

    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, 'x');

    std::promise<bool> p;
    auto f = p.get_future();
    auto moduleCallback = [&p, dstNodeID, data](bcos::crypto::NodeIDPtr _nodeID,
                              const std::string& _id, bytesConstRef _data) {
        BOOST_CHECK(!_id.empty());
        BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
        BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
        p.set_value(true);
    };

    int moduleID = 111;
    frontService->registerModuleMessageDispatcher(moduleID, moduleCallback);
    BOOST_CHECK(frontService->moduleID2MessageDispatcher().find(moduleID) !=
                frontService->moduleID2MessageDispatcher().end());

    frontService->asyncSendMessageByNodeID(moduleID, dstNodeID,
        bytesConstRef((unsigned char*)data.data(), data.size()), 0, CallbackFunc());
    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_onRecieveNodeIDsAnd)
{
    auto frontService = buildFrontService();
    int moduleID = 1000;
    std::promise<bool> p;
    auto f = p.get_future();
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::shared_ptr<crypto::NodeIDs> nodeIDs = std::make_shared<crypto::NodeIDs>();
    nodeIDs->push_back(dstNodeID);
    nodeIDs->push_back(dstNodeID);

    std::shared_ptr<const crypto::NodeIDs> nodeIDs0;
    frontService->registerModuleNodeIDsDispatcher(
        moduleID, [&p, &nodeIDs0](std::shared_ptr<const crypto::NodeIDs> _nodeIDs,
                      ReceiveMsgFunc _receiveMsgCallback) {
            nodeIDs0 = _nodeIDs;
            p.set_value(true);
            if (_receiveMsgCallback)
            {
                _receiveMsgCallback(nullptr);
            }
        });

    BOOST_CHECK(frontService->moduleID2NodeIDsDispatcher().find(moduleID) !=
                frontService->moduleID2NodeIDsDispatcher().end());
    BOOST_CHECK(frontService->moduleID2NodeIDsDispatcher().find(moduleID + 1) ==
                frontService->moduleID2NodeIDsDispatcher().end());

    frontService->onReceiveNodeIDs(
        "1", nodeIDs, [](Error::Ptr _error) { BOOST_CHECK(_error == nullptr); });

    f.get();
    BOOST_CHECK(nodeIDs0->size() == nodeIDs->size());
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendMessageByNodeID_callback)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());

    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(100000, '#');
    int moduleID = 12345;

    {
        std::promise<bool> p;
        auto f = p.get_future();
        auto callback = [dstNodeID, data, &p](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
                            bytesConstRef _data, const std::string& _uuid,
                            std::function<void(bytesConstRef _respData)> _respFunc) {
            (void)_uuid;
            (void)_respFunc;
            BOOST_CHECK(_error == nullptr);
            BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
            BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
            p.set_value(true);
        };
        frontService->asyncSendMessageByNodeID(moduleID, dstNodeID,
            bytesConstRef((unsigned char*)data.data(), data.size()), 0, callback);
        BOOST_CHECK(!frontService->callback().empty());
        auto uuid = frontService->callback().begin()->first;
        frontService->asyncSendResponse(uuid, moduleID, dstNodeID,
            bytesConstRef((unsigned char*)data.data(), data.size()),
            [](Error::Ptr _error) { (void)_error; });
        f.get();
        BOOST_CHECK(frontService->callback().empty());
    }
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendMessageByNodeIDcmak_timeout)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());
    auto message = frontService->messageFactory()->buildMessage();

    int moduleID = 222;
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(100000, '#');

    BOOST_CHECK(frontService->callback().empty());

    {
        std::promise<void> barrier;
        Error::Ptr _error;
        auto callback = [&](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
                            const std::string& _uuid,
                            std::function<void(bytesConstRef _respData)> _respFunc) {
            (void)_nodeID;
            (void)_data;
            (void)_respFunc;
            (void)_uuid;
            BOOST_CHECK_EQUAL(_error->errorCode(), bcos::protocol::CommonError::TIMEOUT);
            barrier.set_value();
        };

        frontService->asyncSendMessageByNodeID(moduleID, dstNodeID,
            bytesConstRef((unsigned char*)data.data(), data.size()), 2000, callback);

        BOOST_CHECK(frontService->callback().size() == 1);
        std::future<void> barrier_future = barrier.get_future();
        barrier_future.wait();
        BOOST_CHECK(frontService->callback().empty());
    }
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendBroadcastMessage)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());

    auto dstNodeID = createKey(g_srcNodeID);
    std::string data(1000, 'x');

    std::promise<bool> p;
    auto f = p.get_future();
    auto moduleCallback = [&p, dstNodeID, data](bcos::crypto::NodeIDPtr _nodeID,
                              const std::string& _id, bytesConstRef _data) {
        (void)_id;
        BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
        BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
        p.set_value(true);
    };

    int moduleID = 111;
    frontService->registerModuleMessageDispatcher(moduleID, moduleCallback);
    BOOST_CHECK(frontService->moduleID2MessageDispatcher().find(moduleID) !=
                frontService->moduleID2MessageDispatcher().end());

    frontService->asyncSendBroadcastMessage(
        moduleID, bytesConstRef((unsigned char*)data.data(), data.size()));
    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendMessageByNodeIDs)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());

    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, 'x');

    std::promise<bool> p;
    auto f = p.get_future();
    auto moduleCallback = [&p, dstNodeID, data](bcos::crypto::NodeIDPtr _nodeID,
                              const std::string& _id, bytesConstRef _data) {
        (void)_id;
        BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
        BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
        p.set_value(true);
    };

    int moduleID = 111;
    frontService->registerModuleMessageDispatcher(moduleID, moduleCallback);
    BOOST_CHECK(frontService->moduleID2MessageDispatcher().find(moduleID) !=
                frontService->moduleID2MessageDispatcher().end());

    frontService->asyncSendMessageByNodeIDs(moduleID, bcos::crypto::NodeIDs{dstNodeID},
        bytesConstRef((unsigned char*)data.data(), data.size()));

    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_loopTimeout)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());
    auto message = frontService->messageFactory()->buildMessage();

    int moduleID = 12345;
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, '#');

    BOOST_CHECK(frontService->callback().empty());

    std::vector<std::promise<void>> barriers;
    barriers.resize(1000);

    for (auto& barrier : barriers)
    {
        Error::Ptr _error;
        auto callback = [&](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
                            const std::string& _uuid,
                            std::function<void(bytesConstRef _respData)> _respFunc) {
            (void)_nodeID;
            (void)_data;
            (void)_uuid;
            (void)_respFunc;
            BOOST_CHECK_EQUAL(_error->errorCode(), bcos::protocol::CommonError::TIMEOUT);
            barrier.set_value();
        };

        frontService->asyncSendMessageByNodeID(moduleID, dstNodeID,
            bytesConstRef((unsigned char*)data.data(), data.size()), 2000, callback);
    }

    BOOST_CHECK(frontService->callback().size() == barriers.size());

    for (auto& barrier : barriers)
    {
        std::future<void> barrier_future = barrier.get_future();
        barrier_future.wait();
    }

    BOOST_CHECK(frontService->callback().empty());
}

BOOST_AUTO_TEST_SUITE_END()
