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

#include "bcos-task/Wait.h"
#define BOOST_TEST_MAIN

#include "FakeGateway.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-front/FrontService.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/single.hpp>
#include <thread>

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
    auto nodeID =
        keyFactory->createKey(bytesConstRef((bcos::byte*)_strNodeID.data(), _strNodeID.size()));
    return nodeID;
}

std::shared_ptr<FrontService> buildFrontService()
{
    auto gateway = std::make_shared<FakeGateway>();
    auto srcNodeID = createKey(g_srcNodeID);
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "frontTest");

    auto frontServiceFactory = std::make_shared<FrontServiceFactory>();
    frontServiceFactory->setGatewayInterface(gateway);
    frontServiceFactory->setIOServicePool(ioServicePool);
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
    BOOST_CHECK(frontService->ioService());
    BOOST_CHECK(frontService->callback().empty());
    BOOST_CHECK(frontService->moduleID2MessageDispatcher().empty());
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_fireAndForget)
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

    // fire-and-forget coroutine send (timeout == 0): the module dispatcher is invoked via the fake
    // gateway's local delivery
    task::syncWait(frontService->sendMessageByNodeID(moduleID, dstNodeID,
        ::ranges::views::single(bytesConstRef((unsigned char*)data.data(), data.size())), 0));
    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_fireAndForget_propagatesGatewayError)
{
    // Round-8 review: the fire-and-forget branch (_timeout == 0) previously returned SendResult{}
    // even when the gateway send failed, so the TARS fire-and-forget reply always encoded SUCCESS.
    // The gateway failure must now be propagated in SendResult::error.
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());
    gateway->setSendError(BCOS_ERROR_PTR(12345, "gateway send failed"));

    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(100, 'x');
    auto result = task::syncWait(frontService->sendMessageByNodeID(111, dstNodeID,
        ::ranges::views::single(bytesConstRef((unsigned char*)data.data(), data.size())), 0));
    BOOST_REQUIRE(result.error);
    BOOST_CHECK_EQUAL(result.error->errorCode(), 12345);
    // nodeID/uuid stay default so the TARS server echoes the request nodeID/seq
    BOOST_CHECK(!result.nodeID);
    BOOST_CHECK(result.uuid.empty());
}

BOOST_AUTO_TEST_CASE(testFrontService_onRecieveNodeIDsAnd)
{
    auto frontService = buildFrontService();
    int moduleID = 1000;
    std::promise<bool> p;
    auto f = p.get_future();
    std::vector<std::string> expectedNodeIDList;
    expectedNodeIDList.emplace_back(g_dstNodeID_0);
    expectedNodeIDList.emplace_back(g_dstNodeID_0);
    auto orgExpectedNodeIDList = expectedNodeIDList;

    std::vector<std::string> nodeIDs0;
    frontService->registerGroupNodeInfoNotification(
        moduleID, [&p, &nodeIDs0](bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
                      ReceiveMsgFunc _receiveMsgCallback) {
            nodeIDs0 = _groupNodeInfo->nodeIDList();
            p.set_value(true);
            if (_receiveMsgCallback)
            {
                _receiveMsgCallback(nullptr);
            }
        });

    auto notifierMap = frontService->module2GroupNodeInfoNotifier();
    BOOST_CHECK(notifierMap.find(moduleID) != notifierMap.end());
    BOOST_CHECK(notifierMap.find(moduleID + 1) == notifierMap.end());

    auto groupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
    groupNodeInfo->setNodeIDList(std::move(expectedNodeIDList));
    frontService->onReceiveGroupNodeInfo(
        "1", groupNodeInfo, [](Error::Ptr _error) { BOOST_CHECK(_error == nullptr); });

    // Use wait_for with timeout to avoid hanging indefinitely on CI
    auto status = f.wait_for(std::chrono::seconds(10));
    BOOST_CHECK_MESSAGE(status == std::future_status::ready,
        "Timed out waiting for group node info notification");
    if (status == std::future_status::ready)
    {
        f.get();
        BOOST_CHECK(nodeIDs0.size() == orgExpectedNodeIDList.size());
    }
}

BOOST_AUTO_TEST_CASE(testFrontService_asyncSendResponse_coroutine)
{
    auto frontService = buildFrontService();
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(100000, '#');
    int moduleID = 12345;

    // the module dispatcher replies through the (kept) asyncSendResponse API, which is now a thin
    // wrapper over the coroutine sendMessage(isResponse=true)
    auto resultPromise = std::make_shared<std::promise<SendResult>>();
    auto resultFuture = resultPromise->get_future();
    frontService->registerModuleMessageDispatcher(moduleID,
        [frontService, dstNodeID, moduleID, data](bcos::crypto::NodeIDPtr _nodeID,
            const std::string& _id, bytesConstRef _data) {
            (void)_nodeID;
            (void)_data;
            frontService->asyncSendResponse(_id, moduleID, dstNodeID,
                bytesConstRef((unsigned char*)data.data(), data.size()),
                [](Error::Ptr _error) { (void)_error; });
        });

    auto self = frontService;
    task::wait(
        [](decltype(self) _self, decltype(dstNodeID) _dstNodeID, int _moduleID, std::string _data,
            std::shared_ptr<std::promise<SendResult>> _resultPromise) -> task::Task<void> {
            auto result = co_await _self->sendMessageByNodeID(_moduleID, _dstNodeID,
                ::ranges::views::single(bytesConstRef(
                    reinterpret_cast<const bcos::byte*>(_data.data()), _data.size())),
                5000);
            _resultPromise->set_value(std::move(result));
        }(self, dstNodeID, moduleID, data, resultPromise));

    auto status = resultFuture.wait_for(std::chrono::seconds(10));
    BOOST_REQUIRE(status == std::future_status::ready);
    auto result = resultFuture.get();
    BOOST_CHECK(!result.error);
    BOOST_CHECK_EQUAL(std::string(result.payload.begin(), result.payload.end()), data);
    BOOST_CHECK(frontService->callback().empty());
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_timeout)
{
    auto frontService = buildFrontService();

    int moduleID = 222;
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(100000, '#');

    BOOST_CHECK(frontService->callback().empty());

    auto result = task::syncWait(frontService->sendMessageByNodeID(moduleID, dstNodeID,
        ::ranges::views::single(bytesConstRef((unsigned char*)data.data(), data.size())), 2000));

    BOOST_REQUIRE(result.error);
    BOOST_CHECK_EQUAL(result.error->errorCode(), bcos::protocol::CommonError::TIMEOUT);
    BOOST_CHECK(frontService->callback().empty());
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

    task::syncWait(
        frontService->broadcastMessage(bcos::protocol::NodeType::CONSENSUS_NODE, moduleID,
            ::ranges::views::single(bytesConstRef((unsigned char*)data.data(), data.size()))));
    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_coroutine)
{
    // Zero-copy coroutine point-to-point: the front encodes only the FrontMessage header into its
    // frame and passes the payload as a view; the (fake) gateway receives the joined wire bytes and
    // delivers them back through the receive loop to the module dispatcher.
    auto frontService = buildFrontService();
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, 'y');

    std::promise<bool> p;
    auto f = p.get_future();
    auto moduleCallback = [&p, dstNodeID, data](bcos::crypto::NodeIDPtr _nodeID,
                              const std::string& _id, bytesConstRef _data) {
        BOOST_CHECK(!_id.empty());
        BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
        BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
        p.set_value(true);
    };

    int moduleID = 222;
    frontService->registerModuleMessageDispatcher(moduleID, moduleCallback);

    // timeout == 0: fire-and-forget send; the module-level dispatch (not a response) is what the
    // fake gateway loops back, so only the send completion matters here.
    auto result = task::syncWait(frontService->sendMessageByNodeID(moduleID, dstNodeID,
        ::ranges::views::single(bytesConstRef(
            reinterpret_cast<const bcos::byte*>(data.data()), data.size())),
        0));
    (void)result;
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_coroutine_withResponse)
{
    // Round-6 review finding 2 (test gap): the response-waiting coroutine path (_timeout > 0) was
    // never covered — the existing coroutine test passes _timeout == 0, which returns before the
    // SendResponseAwaitable is even constructed. This drives the real response path end-to-end:
    // the module dispatcher receives the request and replies via sendMessage(isResponse=true), the
    // fake gateway loops the response back, and handleCallback completes the registered
    // SendResponseAwaitable, which resumes the suspended coroutine with the SendResult.
    auto frontService = buildFrontService();
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, 'z');
    std::string responsePayload(64, 'R');

    // Round-7 review: keep the promise alive via shared_ptr. The detached coroutine holds a raw
    // reference to it; if wait_for below ever timed out, BOOST_REQUIRE aborts the test and the
    // stack promise would be destroyed while the coroutine may still call set_value. A shared_ptr
    // keeps the promise alive either way.
    auto resultPromise = std::make_shared<std::promise<SendResult>>();
    auto resultFuture = resultPromise->get_future();

    int moduleID = 333;
    frontService->registerModuleMessageDispatcher(moduleID,
        [&](bcos::crypto::NodeIDPtr _nodeID, const std::string& _id, bytesConstRef _data) {
            BOOST_CHECK_EQUAL(dstNodeID->hex(), _nodeID->hex());
            BOOST_CHECK_EQUAL(std::string(_data.begin(), _data.end()), data);
            // reply with an isResponse=true frame carrying the same uuid; the fake gateway loops
            // it back to onReceiveMessage, where message.isResponse() triggers handleCallback ->
            // SendResponseAwaitable::complete
            frontService->sendMessage(moduleID, dstNodeID, _id,
                bytesConstRef(reinterpret_cast<const bcos::byte*>(responsePayload.data()),
                    responsePayload.size()),
                true, nullptr);
        });

    auto self = frontService;
    task::wait(
        [](decltype(self) _self, decltype(dstNodeID) _dstNodeID, int _moduleID, std::string _data,
            std::shared_ptr<std::promise<SendResult>> _resultPromise) -> task::Task<void> {
            auto result = co_await _self->sendMessageByNodeID(_moduleID, _dstNodeID,
                ::ranges::views::single(bytesConstRef(
                    reinterpret_cast<const bcos::byte*>(_data.data()), _data.size())),
                5000);
            _resultPromise->set_value(std::move(result));
        }(self, dstNodeID, moduleID, data, resultPromise));

    auto status = resultFuture.wait_for(std::chrono::seconds(10));
    BOOST_REQUIRE(status == std::future_status::ready);
    auto result = resultFuture.get();
    BOOST_CHECK(!result.error);
    BOOST_CHECK(!result.uuid.empty());
    BOOST_CHECK_EQUAL(std::string(result.payload.begin(), result.payload.end()), responsePayload);
    BOOST_CHECK(result.respond);
}

BOOST_AUTO_TEST_CASE(testFrontService_sendMessageByNodeID_toNode)
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

    task::syncWait(frontService->sendMessageByNodeID(moduleID, dstNodeID,
        ::ranges::views::single(bytesConstRef((unsigned char*)data.data(), data.size())), 0));

    BOOST_CHECK(frontService->callback().empty());
    f.get();
}

BOOST_AUTO_TEST_CASE(testFrontService_loopTimeout)
{
    auto frontService = buildFrontService();
    auto gateway = std::static_pointer_cast<FakeGateway>(frontService->gatewayInterface());

    int moduleID = 12345;
    auto dstNodeID = createKey(g_dstNodeID_0);
    std::string data(1000, '#');

    BOOST_CHECK(frontService->callback().empty());

    std::vector<std::promise<void>> barriers;
    barriers.resize(1000);

    std::vector<std::thread> senders;
    senders.reserve(barriers.size());
    for (auto& barrier : barriers)
    {
        senders.emplace_back([frontService, moduleID, dstNodeID, data, &barrier]() {
            auto result = task::syncWait(frontService->sendMessageByNodeID(moduleID, dstNodeID,
                ::ranges::views::single(
                    bytesConstRef((unsigned char*)data.data(), data.size())),
                2000));
            BOOST_CHECK(result.error);
            if (result.error)
            {
                BOOST_CHECK_EQUAL(
                    result.error->errorCode(), bcos::protocol::CommonError::TIMEOUT);
            }
            barrier.set_value();
        });
    }

    for (auto& t : senders)
    {
        t.join();
    }

    BOOST_CHECK(frontService->callback().empty());
}

BOOST_AUTO_TEST_SUITE_END()
