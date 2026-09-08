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
 * @brief Unit tests for Gateway class
 * @file GatewayTest.cpp
 * @author: MO NAN
 * @date 2025-09-25
 */

#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-crypto/signature/key/KeyFactoryImpl.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/rpc/RPCInterface.h"
#include "bcos-gateway/Gateway.h"
#include "bcos-gateway/gateway/GatewayNodeManager.h"
#include "bcos-gateway/libamop/AMOPImpl.h"
#include "bcos-gateway/libamop/TopicManager.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <memory>

using namespace fakeit;
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::test;
using namespace bcos::group;

BOOST_AUTO_TEST_SUITE(GatewayUnitTest)

namespace
{
// Hand-written counting fake replacing the former FakeIt Mock<P2PInterface> (the interface is
// gone; Service is the single concrete p2p service).
class CountingP2PService : public Service
{
public:
    CountingP2PService() : Service(P2PInfo()) {}

    void start() override { ++startCalls; }
    void stop() override { ++stopCalls; }

    int startCalls = 0;
    int stopCalls = 0;
};

// Exposes the protected "for ut" constructor so a real GatewayNodeManager can be driven without a
// p2p service.
class TestNodeManager : public GatewayNodeManager
{
public:
    TestNodeManager() : GatewayNodeManager("", std::make_shared<KeyFactoryImpl>(), nullptr) {}
};

ProtocolInfo::ConstPtr makeTestProtocolInfo()
{
    return std::make_shared<ProtocolInfo>(
        ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1);
}

// Real AMOPImpl over a local-mode TopicManager whose local RPC client is a FakeIt mock of the
// (still abstract) RPCInterface -- this replaces the former Mock<AMOPImpl>: the assertions now run
// against the real AMOPImpl topic-dispatch path and the mock only scripts the RPC client's
// notifyAMOPMessage responses.
struct AMOPTestContext
{
    AMOPTestContext()
    {
        topicManager =
            std::make_shared<amop::TopicManager>("gatewayTest", service, /*_localMode=*/true);
        topicManager->setLocalClient(
            bcos::rpc::RPCInterface::Ptr(&rpcMock.get(), [](bcos::rpc::RPCInterface*) {}));
        ioServicePool = std::make_shared<bcos::IOServicePool>(1, "gatewayTest");
        amop = std::make_shared<amop::AMOPImpl>(topicManager,
            std::make_shared<amop::AMOPMessageFactory>(),
            std::make_shared<bcos::protocol::AMOPRequestFactory>(), service, localNodeID, ioContext,
            ioServicePool);
    }

    std::shared_ptr<CountingP2PService> service = std::make_shared<CountingP2PService>();
    fakeit::Mock<bcos::rpc::RPCInterface> rpcMock;
    amop::TopicManager::Ptr topicManager;
    boost::asio::io_context ioContext;
    bcos::IOServicePool::Ptr ioServicePool;
    std::shared_ptr<amop::AMOPImpl> amop;
    P2pID localNodeID = std::string(128, 'f');
};
}  // namespace

struct GatewayTestFixture : public TestPromptFixture
{
    GatewayTestFixture()
    {
        // Create key factory and node ID
        keyFactory = std::make_shared<KeyFactoryImpl>();
        const size_t NODE_ID_SIZE = 32;
        nodeID = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));

        setupMocks();
    }

    void setupMocks()
    {
        // Create simple test gateway without complex dependencies
        // We'll focus on testing methods that don't require heavy mocking first
        testGateway = std::make_shared<TestableGateway>();
    }

    class TestableGateway : public Gateway
    {
    public:
        TestableGateway() = default;

        // Expose protected methods for testing
        using Gateway::checkGroupInfo;

        // Test tracking variables
        bool startCalled = false;
        bool stopCalled = false;

        // Override methods for testing
        void start() override { startCalled = true; }

        void stop() override { stopCalled = true; }
    };

    std::shared_ptr<TestableGateway> testGateway;
    std::shared_ptr<KeyFactoryImpl> keyFactory;
    NodeIDPtr nodeID;
};

BOOST_AUTO_TEST_CASE(testGatewayConstruction)
{
    GatewayTestFixture fixture;
    BOOST_CHECK(fixture.testGateway != nullptr);
    BOOST_CHECK(fixture.keyFactory != nullptr);
    BOOST_CHECK(fixture.nodeID != nullptr);
}

BOOST_AUTO_TEST_CASE(testStartStop)
{
    GatewayTestFixture fixture;

    // Test start method override
    BOOST_CHECK(fixture.testGateway->startCalled == false);
    fixture.testGateway->start();
    BOOST_CHECK(fixture.testGateway->startCalled == true);

    // Test stop method override
    BOOST_CHECK(fixture.testGateway->stopCalled == false);
    fixture.testGateway->stop();
    BOOST_CHECK(fixture.testGateway->stopCalled == true);
}

BOOST_AUTO_TEST_CASE(testBasicGatewayFunctionality)
{
    GatewayTestFixture fixture;

    // Test basic construction and key factory usage
    BOOST_CHECK(fixture.testGateway != nullptr);
    BOOST_CHECK(fixture.keyFactory != nullptr);
    BOOST_CHECK(fixture.nodeID != nullptr);

    // Test node ID creation
    const size_t NODE_ID_SIZE = 32;
    auto nodeID2 = fixture.keyFactory->createKey(bytes(NODE_ID_SIZE, 0x2));
    BOOST_CHECK(nodeID2 != nullptr);
    BOOST_CHECK(nodeID2->hex() != fixture.nodeID->hex());

    // Test hex encoding consistency
    std::string hexStr = fixture.nodeID->hex();
    BOOST_CHECK(!hexStr.empty());
    BOOST_CHECK(hexStr.length() > 0);
}

BOOST_AUTO_TEST_CASE(testGatewayMockBehavior)
{
    GatewayTestFixture fixture;

    // Test the mock start/stop behavior
    BOOST_CHECK(fixture.testGateway->startCalled == false);
    BOOST_CHECK(fixture.testGateway->stopCalled == false);

    fixture.testGateway->start();
    BOOST_CHECK(fixture.testGateway->startCalled == true);

    fixture.testGateway->stop();
    BOOST_CHECK(fixture.testGateway->stopCalled == true);
}

BOOST_AUTO_TEST_CASE(testGatewayObjectCreation)
{
    // Test that we can create all the required objects for Gateway
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    BOOST_CHECK(keyFactory != nullptr);

    // Test node ID creation and operations
    const size_t NODE_ID_SIZE = 32;
    auto nodeID1 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));
    auto nodeID2 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x2));

    BOOST_CHECK(nodeID1 != nullptr);
    BOOST_CHECK(nodeID2 != nullptr);
    BOOST_CHECK(nodeID1->hex() != nodeID2->hex());

    // Test that hex encoding is consistent
    std::string hex1 = nodeID1->hex();
    std::string hex2 = nodeID2->hex();

    BOOST_CHECK(!hex1.empty());
    BOOST_CHECK(!hex2.empty());
    BOOST_CHECK_EQUAL(hex1, nodeID1->hex());  // Should be consistent
}

BOOST_AUTO_TEST_CASE(testP2PServiceStartStop)
{
    // Formerly testFakeItWithInterfaces / testGatewayP2PInterfaceMock (FakeIt Mock<P2PInterface>):
    // the P2PInterface mock is replaced by a hand-written counting Service subclass; the assertion
    // intent (start/stop callable, each invoked exactly once) is preserved as counter assertions.
    auto service = std::make_shared<CountingP2PService>();

    BOOST_CHECK_NO_THROW(service->start());
    BOOST_CHECK_NO_THROW(service->stop());

    BOOST_CHECK_EQUAL(service->startCalls, 1);
    BOOST_CHECK_EQUAL(service->stopCalls, 1);
}

BOOST_AUTO_TEST_CASE(testGatewayWithFakeIt)
{
    // Test Gateway methods using fakeit for more complex scenarios
    GatewayTestFixture fixture;

    // Test that our testable gateway can be used for more complex scenarios
    BOOST_CHECK(fixture.testGateway != nullptr);

    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i)
    {
        fixture.testGateway->start();
        BOOST_CHECK(fixture.testGateway->startCalled == true);

        fixture.testGateway->stop();
        BOOST_CHECK(fixture.testGateway->stopCalled == true);

        // Reset for next iteration
        fixture.testGateway->startCalled = false;
        fixture.testGateway->stopCalled = false;
    }
}

BOOST_AUTO_TEST_CASE(testNodeIDOperations)
{
    GatewayTestFixture fixture;

    // Test various node ID operations
    const size_t NODE_ID_SIZE = 32;

    // Create multiple test node IDs
    std::vector<KeyInterface::Ptr> nodeIDs;
    const int MAX_NODE_IDS = 5;
    for (int i = 0; i < MAX_NODE_IDS; ++i)
    {
        auto nodeID = fixture.keyFactory->createKey(bytes(NODE_ID_SIZE, i + 1));
        BOOST_CHECK(nodeID != nullptr);
        nodeIDs.push_back(nodeID);
    }

    // Test that all node IDs are different
    for (size_t i = 0; i < nodeIDs.size(); ++i)
    {
        for (size_t j = i + 1; j < nodeIDs.size(); ++j)
        {
            BOOST_CHECK(nodeIDs[i]->hex() != nodeIDs[j]->hex());
        }
    }

    // Test hex string properties
    for (const auto& nodeID : nodeIDs)
    {
        std::string hex = nodeID->hex();
        BOOST_CHECK(!hex.empty());
        BOOST_CHECK(hex.length() > 0);
        BOOST_CHECK(hex == nodeID->hex());  // Consistency check
    }
}

BOOST_AUTO_TEST_CASE(testGatewayNodeManagerRegister)
{
    // Formerly FakeIt Mock<GatewayNodeManager>: now drives a real GatewayNodeManager (built via
    // the protected for-ut constructor, without a p2p service) and asserts the real routing-table
    // state instead of the mock Verify call counts.
    auto nodeManager = std::make_shared<TestNodeManager>();

    std::string testGroupID = "testGroup";
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    const size_t NODE_ID_SIZE = 32;
    auto testNodeID = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));

    // Test node registration
    bool registerResult = nodeManager->registerNode(
        testGroupID, testNodeID, NodeType::CONSENSUS_NODE, nullptr, makeTestProtocolInfo());
    BOOST_CHECK(registerResult);

    // the node is now visible in the local router table; re-registering the same node fails
    auto nodeList = nodeManager->localRouterTable()->nodeList();
    BOOST_REQUIRE_EQUAL(nodeList.count(testGroupID), 1U);
    BOOST_CHECK(nodeList.at(testGroupID).count(testNodeID->hex()) == 1U);
    BOOST_CHECK(!nodeManager->registerNode(
        testGroupID, testNodeID, NodeType::CONSENSUS_NODE, nullptr, makeTestProtocolInfo()));

    // Test node unregistration
    bool unregisterResult = nodeManager->unregisterNode(testGroupID, testNodeID->hex());
    BOOST_CHECK(unregisterResult);
    BOOST_CHECK(nodeManager->localRouterTable()->nodeList().count(testGroupID) == 0U);
    // unregistering a removed node fails
    BOOST_CHECK(!nodeManager->unregisterNode(testGroupID, testNodeID->hex()));
}

BOOST_AUTO_TEST_CASE(testAMOPSubscribeAndLocalDelivery)
{
    // Formerly FakeIt Mock<AMOPImpl> (sendMessageByTopic / asyncSubscribeTopic scripted tuples):
    // the mock is replaced by a real AMOPImpl whose topicManager runs in local (Air) mode with a
    // mocked RPCInterface client, so the assertions exercise the real local-delivery path.
    AMOPTestContext ctx;

    bcos::bytes responsePayload{0x4, 0x5, 0x6};
    fakeit::When(Method(ctx.rpcMock, notifyAMOPMessage))
        .AlwaysDo([responsePayload](int16_t, std::string const&, bytesConstRef)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> {
            co_return std::make_tuple(
                bcos::Error::Ptr(nullptr), std::make_shared<bcos::bytes>(responsePayload));
        });

    std::string testTopic = "testTopic";
    std::string testClientID = "testClient";
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // Test topic subscription: the callback fires with a null error and the topic is registered
    bool subscriptionCallbackInvoked = false;
    ctx.amop->asyncSubscribeTopic(testClientID, R"({"topics":["testTopic"]})",
        [&subscriptionCallbackInvoked](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            subscriptionCallbackInvoked = true;
        });
    BOOST_CHECK(subscriptionCallbackInvoked);

    amop::TopicItems topicItems;
    BOOST_REQUIRE(ctx.topicManager->queryTopicItemsByClient(testClientID, topicItems));
    BOOST_CHECK_EQUAL(topicItems.size(), 1U);

    // Test message sending: no remote subscriber, so it is delivered to the local client
    auto [sendError, sendCode, sendResponse] = bcos::task::syncWait(ctx.amop->sendMessageByTopic(
        testTopic, bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(!sendError);
    BOOST_CHECK_EQUAL(sendCode, GatewayMessageType::WSMessageType);
    BOOST_CHECK(sendResponse == responsePayload);

    // the local RPC client was notified exactly once
    fakeit::Verify(Method(ctx.rpcMock, notifyAMOPMessage)).Exactly(1);

    // a topic without any subscriber fails fast
    auto [noSubError, noSubCode, noSubResponse] = bcos::task::syncWait(
        ctx.amop->sendMessageByTopic("topic_without_subscriber",
            bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(noSubError != nullptr);
    BOOST_CHECK_EQUAL(noSubError->errorCode(), CommonError::NotFoundPeerByTopicSendMsg);
    BOOST_CHECK(noSubResponse.empty());

    fakeit::Verify(Method(ctx.rpcMock, notifyAMOPMessage)).Exactly(1);
}

BOOST_AUTO_TEST_CASE(testComplexGatewayScenario)
{
    // Test a more complex scenario using the real components together (formerly three FakeIt
    // mocks): counting p2p service, real GatewayNodeManager, real AMOPImpl with a mocked local
    // RPC client that answers per topic.
    auto service = std::make_shared<CountingP2PService>();
    auto nodeManager = std::make_shared<TestNodeManager>();
    AMOPTestContext ctx;

    fakeit::When(Method(ctx.rpcMock, notifyAMOPMessage))
        .AlwaysDo([](int16_t, std::string const& topic, bytesConstRef)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> {
            // Simulate different responses based on topic
            if (topic == "failTopic")
            {
                auto error = std::make_shared<bcos::Error>(
                    bcos::Error::buildError("MockTest", -1, "Mock error for test"));
                co_return std::make_tuple(std::move(error), bytesPointer{});
            }
            co_return std::make_tuple(
                bcos::Error::Ptr(nullptr), std::make_shared<bcos::bytes>());
        });

    // Simulate a complete Gateway workflow
    service->start();  // Start P2P service
    BOOST_CHECK_EQUAL(service->startCalls, 1);

    // Register some nodes
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    const size_t NODE_ID_SIZE = 32;
    auto nodeID1 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));
    auto nodeID2 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x2));

    bool reg1 = nodeManager->registerNode(
        "group1", nodeID1, NodeType::CONSENSUS_NODE, nullptr, makeTestProtocolInfo());
    bool reg2 = nodeManager->registerNode(
        "group1", nodeID2, NodeType::CONSENSUS_NODE, nullptr, makeTestProtocolInfo());

    BOOST_CHECK(reg1);
    BOOST_CHECK(reg2);
    BOOST_CHECK_EQUAL(nodeManager->localRouterTable()->nodeList().at("group1").size(), 2U);

    // Test AMOP operations with different outcomes
    bcos::bytes testData = {0x1, 0x2, 0x3};
    std::string testClientID = "testClient";
    ctx.amop->asyncSubscribeTopic(
        testClientID, R"({"topics":["successTopic","failTopic"]})", [](bcos::Error::Ptr&&) {});

    // Test successful case
    auto [successError, successCode, successResponse] =
        bcos::task::syncWait(ctx.amop->sendMessageByTopic(
            "successTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(!successError);
    BOOST_CHECK_EQUAL(successCode, GatewayMessageType::WSMessageType);
    BOOST_CHECK(successResponse.empty());

    // Test failure case: the local client reports an error, surfaced as an AMOP error response.
    // The error code crosses the wire through the AMOPMessage uint16 status field, so -1 arrives
    // as 65535; the raw response payload is the encoded AMOPMessage (status + error message).
    auto [failError, failCode, failResponse] = bcos::task::syncWait(ctx.amop->sendMessageByTopic(
        "failTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(failError != nullptr);
    BOOST_CHECK_EQUAL(failError->errorCode(), (int)(uint16_t)(-1));
    BOOST_CHECK_EQUAL(failError->errorMessage(), "Mock error for test");
    BOOST_CHECK_EQUAL(failCode, GatewayMessageType::AMOPMessageType);
    BOOST_CHECK(!failResponse.empty());

    // Clean up - unregister nodes
    bool unreg1 = nodeManager->unregisterNode("group1", nodeID1->hex());
    bool unreg2 = nodeManager->unregisterNode("group1", nodeID2->hex());

    BOOST_CHECK(unreg1);
    BOOST_CHECK(unreg2);
    BOOST_CHECK(nodeManager->localRouterTable()->nodeList().count("group1") == 0U);

    // all expected interactions occurred
    fakeit::Verify(Method(ctx.rpcMock, notifyAMOPMessage)).Exactly(2);
}

BOOST_AUTO_TEST_CASE(testErrorHandlingScenarios)
{
    // Sequential behavior (formerly a FakeIt Mock<AMOPImpl> with a call-count switch): the local
    // RPC client mock fails from the second call on, and the real AMOPImpl surfaces that error.
    AMOPTestContext ctx;

    int callCount = 0;
    fakeit::When(Method(ctx.rpcMock, notifyAMOPMessage))
        .AlwaysDo([&callCount](int16_t, std::string const&, bytesConstRef)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> {
            ++callCount;
            if (callCount == 1)
            {
                // First call succeeds
                co_return std::make_tuple(
                    bcos::Error::Ptr(nullptr), std::make_shared<bcos::bytes>());
            }
            // Subsequent calls fail
            auto error = std::make_shared<bcos::Error>(
                bcos::Error::buildError("MockTest", -2, "Network timeout"));
            co_return std::make_tuple(std::move(error), bytesPointer{});
        });

    ctx.amop->asyncSubscribeTopic(
        "testClient", R"({"topics":["testTopic"]})", [](bcos::Error::Ptr&&) {});

    bcos::bytes testData = {0x1, 0x2, 0x3};

    // First call should succeed
    auto [firstError, firstCode, firstResponse] = bcos::task::syncWait(
        ctx.amop->sendMessageByTopic("testTopic", bcos::bytesConstRef(testData.data(),
            testData.size())));
    BOOST_CHECK(firstError == nullptr && firstCode == GatewayMessageType::WSMessageType);
    BOOST_CHECK(firstResponse.empty());

    // Second call should fail: the local client error is encoded into the AMOPMessage uint16
    // status (-2 -> 65534), and the response payload is the encoded error message frame.
    auto [secondError, secondCode, secondResponse] = bcos::task::syncWait(
        ctx.amop->sendMessageByTopic("testTopic", bcos::bytesConstRef(testData.data(),
            testData.size())));
    BOOST_CHECK(secondError != nullptr && secondError->errorCode() == (int)(uint16_t)(-2));
    BOOST_CHECK_EQUAL(secondError->errorMessage(), "Network timeout");
    BOOST_CHECK_EQUAL(secondCode, GatewayMessageType::AMOPMessageType);
    BOOST_CHECK(!secondResponse.empty());

    // both calls reached the local RPC client
    BOOST_CHECK_EQUAL(callCount, 2);
    fakeit::Verify(Method(ctx.rpcMock, notifyAMOPMessage)).Exactly(2);
}

BOOST_AUTO_TEST_SUITE_END()
