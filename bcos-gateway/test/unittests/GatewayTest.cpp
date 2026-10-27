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

#include "bcos-gateway/Gateway.h"
#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-crypto/signature/key/KeyFactoryImpl.h"
#include "bcos-framework/front/FrontServiceInterface.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-gateway/gateway/GatewayNodeManager.h"
#include "bcos-gateway/gateway/LocalRouterTable.h"
#include "bcos-gateway/libamop/AMOPImpl.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-task/OneShot.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <memory>
#include <utility>
#include <vector>

using namespace fakeit;
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::test;
using namespace bcos::group;

// Forward declarations for interfaces we'll mock
namespace bcos::gateway
{
class GatewayNodeManager;
}  // namespace bcos::gateway
namespace bcos::amop
{
class AMOPImpl;
}

BOOST_AUTO_TEST_SUITE(GatewayUnitTest)

struct GatewayTestFixture : public TestPromptFixture
{
    GatewayTestFixture()
    {
        // Create key factory and node ID
        keyFactory = std::make_shared<KeyFactoryImpl>();
        const size_t NODE_ID_SIZE = 32;
        nodeID = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));

        // Create mocks using fakeit
        // Note: We need to be careful about mock lifetime and shared_ptr usage
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

BOOST_AUTO_TEST_CASE(testFakeItWithInterfaces)
{
    // Test fakeit usage similar to testBaselineScheduler.cpp
    // Create mocks for Gateway dependencies

    // Mock Service for testing P2P operations
    fakeit::Mock<Service> mockP2PInterface;

    // Setup mock behaviors using When().AlwaysDo() pattern like in testBaselineScheduler
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock start behavior - just return successfully
    });

    fakeit::When(Method(mockP2PInterface, stop)).AlwaysDo([]() {
        // Mock stop behavior - just return successfully
    });

    // Test that we can call the mocked methods
    Service& p2pRef = mockP2PInterface.get();

    // These should not throw since we've mocked them
    BOOST_CHECK_NO_THROW(p2pRef.start());
    BOOST_CHECK_NO_THROW(p2pRef.stop());

    // Verify the methods were called (like in testBaselineScheduler)
    fakeit::Verify(Method(mockP2PInterface, start)).Exactly(1);
    fakeit::Verify(Method(mockP2PInterface, stop)).Exactly(1);
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

BOOST_AUTO_TEST_CASE(testGatewayP2PInterfaceMock)
{
    // Test Service mocking similar to testBaselineScheduler pattern
    // Service is polymorphic and can be mocked
    fakeit::Mock<Service> mockP2PInterface;

    // Setup mock behaviors using When().AlwaysDo() pattern
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock successful start
    });
    fakeit::When(Method(mockP2PInterface, stop)).AlwaysDo([]() {
        // Mock successful stop
    });

    // Test the mocked P2P interface
    Service& p2pRef = mockP2PInterface.get();

    // These should not throw since we've mocked them
    BOOST_CHECK_NO_THROW(p2pRef.start());
    BOOST_CHECK_NO_THROW(p2pRef.stop());

    // Verify methods were called
    fakeit::Verify(Method(mockP2PInterface, start)).Exactly(1);
    fakeit::Verify(Method(mockP2PInterface, stop)).Exactly(1);
}

BOOST_AUTO_TEST_CASE(testGatewayNodeManagerMock)
{
    // Test GatewayNodeManager mocking
    fakeit::Mock<GatewayNodeManager> mockGatewayNodeManager;

    // Setup mock behaviors for node registration
    fakeit::When(Method(mockGatewayNodeManager, registerNode)).AlwaysReturn(true);
    fakeit::When(Method(mockGatewayNodeManager, unregisterNode)).AlwaysReturn(true);

    // Test the mocked node manager
    GatewayNodeManager& nodeManagerRef = mockGatewayNodeManager.get();

    // Create test parameters
    std::string testGroupID = "testGroup";
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    const size_t NODE_ID_SIZE = 32;
    auto testNodeID = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));

    // Test node registration
    bool registerResult = nodeManagerRef.registerNode(
        testGroupID, testNodeID, NodeType::CONSENSUS_NODE, nullptr, nullptr);
    BOOST_CHECK(registerResult);

    // Test node unregistration
    bool unregisterResult = nodeManagerRef.unregisterNode(testGroupID, testNodeID->hex());
    BOOST_CHECK(unregisterResult);

    // Verify methods were called
    fakeit::Verify(Method(mockGatewayNodeManager, registerNode)).Exactly(1);
    fakeit::Verify(Method(mockGatewayNodeManager, unregisterNode)).Exactly(1);
}

BOOST_AUTO_TEST_CASE(testAMOPMock)
{
    // Test AMOP interface mocking
    fakeit::Mock<bcos::amop::AMOPImpl> mockAMOP;

    // Setup mock behaviors for AMOP operations using lambda pattern from testBaselineScheduler
    fakeit::When(Method(mockAMOP, sendMessageByTopic))
        .AlwaysDo([](const std::string& /*topic*/, bcos::bytesConstRef /*data*/)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>> {
            // Simulate successful message send
            co_return std::make_tuple(bcos::Error::Ptr(nullptr), int16_t(0), bcos::bytes{});
        });

    fakeit::When(Method(mockAMOP, subscribeTopic))
        .AlwaysDo([](std::string const& /*clientID*/,
                      std::string const& /*topicInfo*/) -> bcos::task::Task<bcos::Error::Ptr> {
            // Simulate successful subscription
            co_return bcos::Error::Ptr(nullptr);
        });

    // Test the mocked AMOP
    bcos::amop::AMOPImpl& amopRef = mockAMOP.get();

    std::string testTopic = "testTopic";
    std::string testClientID = "testClient";
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // Test message sending
    auto [sendError, sendCode, sendResponse] = bcos::task::syncWait(amopRef.sendMessageByTopic(
        testTopic, bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(!sendError);
    BOOST_CHECK_EQUAL(sendCode, 0);
    BOOST_CHECK(sendResponse.empty());

    // Test topic subscription
    auto subscribeError = bcos::task::syncWait(amopRef.subscribeTopic(testClientID, testTopic));
    BOOST_CHECK(!subscribeError);

    // Verify methods were called
    fakeit::Verify(Method(mockAMOP, sendMessageByTopic)).Exactly(1);
    fakeit::Verify(Method(mockAMOP, subscribeTopic)).Exactly(1);
}

BOOST_AUTO_TEST_CASE(testComplexGatewayScenario)
{
    // Test a more complex scenario using multiple mocks together
    // This demonstrates how to build comprehensive tests similar to testBaselineScheduler.cpp

    // Create multiple mocks for a complete Gateway test
    fakeit::Mock<Service> mockP2PInterface;
    fakeit::Mock<GatewayNodeManager> mockGatewayNodeManager;
    fakeit::Mock<bcos::amop::AMOPImpl> mockAMOP;

    // Setup complex mock behaviors
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock P2P start - simulate successful initialization
    });

    fakeit::When(Method(mockGatewayNodeManager, registerNode)).AlwaysReturn(true);
    fakeit::When(Method(mockGatewayNodeManager, unregisterNode)).AlwaysReturn(true);

    // Setup AMOP with multiple different behaviors for different calls
    fakeit::When(Method(mockAMOP, sendMessageByTopic))
        .AlwaysDo([](const std::string& topic, bcos::bytesConstRef /*data*/)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>> {
            // Simulate different responses based on topic
            if (topic == "failTopic")
            {
                auto error = std::make_shared<bcos::Error>(
                    bcos::Error::buildError("MockTest", -1, "Mock error for test"));
                co_return std::make_tuple(std::move(error), int16_t(-1), bcos::bytes{});
            }
            co_return std::make_tuple(bcos::Error::Ptr(nullptr), int16_t(0), bcos::bytes{});
        });

    // Test the complex scenario
    Service& p2pRef = mockP2PInterface.get();
    GatewayNodeManager& nodeManagerRef = mockGatewayNodeManager.get();
    bcos::amop::AMOPImpl& amopRef = mockAMOP.get();

    // Simulate a complete Gateway workflow
    p2pRef.start();  // Start P2P interface

    // Register some nodes
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    const size_t NODE_ID_SIZE = 32;
    auto nodeID1 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x1));
    auto nodeID2 = keyFactory->createKey(bytes(NODE_ID_SIZE, 0x2));

    bool reg1 =
        nodeManagerRef.registerNode("group1", nodeID1, NodeType::CONSENSUS_NODE, nullptr, nullptr);
    bool reg2 =
        nodeManagerRef.registerNode("group1", nodeID2, NodeType::CONSENSUS_NODE, nullptr, nullptr);

    BOOST_CHECK(reg1);
    BOOST_CHECK(reg2);

    // Test AMOP operations with different outcomes
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // Test successful case
    auto [successError, successCode, successResponse] =
        bcos::task::syncWait(amopRef.sendMessageByTopic(
            "successTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(!successError);
    BOOST_CHECK_EQUAL(successCode, 0);
    BOOST_CHECK(successResponse.empty());

    // Test failure case
    auto [failError, failCode, failResponse] = bcos::task::syncWait(amopRef.sendMessageByTopic(
        "failTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(failError != nullptr);
    BOOST_CHECK_EQUAL(failCode, -1);
    BOOST_CHECK(failResponse.empty());

    // Clean up - unregister nodes
    bool unreg1 = nodeManagerRef.unregisterNode("group1", nodeID1->hex());
    bool unreg2 = nodeManagerRef.unregisterNode("group1", nodeID2->hex());

    BOOST_CHECK(unreg1);
    BOOST_CHECK(unreg2);

    // Verify all expected interactions occurred
    fakeit::Verify(Method(mockP2PInterface, start)).Exactly(1);
    fakeit::Verify(Method(mockGatewayNodeManager, registerNode)).Exactly(2);
    fakeit::Verify(Method(mockGatewayNodeManager, unregisterNode)).Exactly(2);
    fakeit::Verify(Method(mockAMOP, sendMessageByTopic)).Exactly(2);
}

BOOST_AUTO_TEST_CASE(testErrorHandlingScenarios)
{
    // Test error handling scenarios using fakeit
    fakeit::Mock<bcos::amop::AMOPImpl> mockAMOP;

    // Setup error scenarios
    int callCount = 0;
    fakeit::When(Method(mockAMOP, sendMessageByTopic))
        .AlwaysDo([&callCount](const std::string& /*topic*/, bcos::bytesConstRef /*data*/)
                      -> bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>> {
            ++callCount;
            if (callCount == 1)
            {
                // First call succeeds
                co_return std::make_tuple(bcos::Error::Ptr(nullptr), int16_t(0), bcos::bytes{});
            }
            // Subsequent calls fail
            auto error = std::make_shared<bcos::Error>(
                bcos::Error::buildError("MockTest", -2, "Network timeout"));
            co_return std::make_tuple(std::move(error), int16_t(-2), bcos::bytes{});
        });

    bcos::amop::AMOPImpl& amopRef = mockAMOP.get();
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // First call should succeed
    auto [firstError, firstCode, firstResponse] = bcos::task::syncWait(amopRef.sendMessageByTopic(
        "testTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(firstError == nullptr && firstCode == 0);
    BOOST_CHECK(firstResponse.empty());

    // Second call should fail
    auto [secondError, secondCode, secondResponse] =
        bcos::task::syncWait(amopRef.sendMessageByTopic(
            "testTopic", bcos::bytesConstRef(testData.data(), testData.size())));
    BOOST_CHECK(secondError != nullptr && secondCode == -2);
    BOOST_CHECK(secondResponse.empty());

    // Verify both calls were made
    fakeit::Verify(Method(mockAMOP, sendMessageByTopic)).Exactly(2);
}

namespace
{
// Round-3 review (F1) fixtures. ParkedSendService makes the response WRITE block until the test
// releases it: the fake parks inside sendRespMessageBySession exactly where production parks —
// the session's write queue behind a stalled peer's socket.
class ParkedSendService : public Service
{
public:
    ParkedSendService()
      : Service([] {
            P2PInfo info;
            info.rawP2pID = "selfRawP2pID";
            info.p2pID = "selfP2pID";
            return info;
        }())
    {}

    task::Task<void> sendRespMessageBySession(bcos::bytes /*payload*/, uint32_t /*seq*/,
        std::string /*srcP2PNodeID*/, P2PSession::Ptr /*session*/) override
    {
        auto gate = std::make_shared<task::OneShot<bool>>();
        sendGates.push_back(gate);
        ++sendStarted;
        co_await *gate;  // outstanding until the test releases it
        ++sendReleased;
    }

    void releaseOne(std::size_t index) { sendGates.at(index)->complete(true); }

    void releaseAll()
    {
        for (auto& gate : sendGates)
        {
            gate->complete(true);
        }
    }

    std::vector<std::shared_ptr<task::OneShot<bool>>> sendGates;
    int sendStarted = 0;
    int sendReleased = 0;
};

// Minimal P2PSession fake: onMessage needs a lockable weak_ptr<P2PSession> and a p2pID for
// logging; no socket is involved.
class FakePeerSession : public P2PSession
{
public:
    explicit FakePeerSession(std::string id) : m_id(std::move(id))
    {
        auto info = mutableP2pInfo();
        info->rawP2pID = m_id;
        info->p2pID = m_id;
    }
    bcos::network::P2pID p2pID() override { return m_id; }
    std::string printP2pID() override { return m_id; }
    std::string m_id;
};

// The UT GatewayNodeManager constructor (uuid, keyFactory, p2pInterface) skips the io_context the
// production one drives timers with; this test never starts those timers.
class TestGatewayNodeManager : public GatewayNodeManager
{
public:
    TestGatewayNodeManager(bcos::crypto::KeyFactory::Ptr keyFactory, Service::Ptr service)
      : GatewayNodeManager("", std::move(keyFactory), std::move(service))
    {}
};

// Records every front-service dispatch; onReceiveMessage completes inline.
class RecordingFrontService : public bcos::front::FrontServiceInterface
{
public:
    void start() override {}
    void stop() override {}
    task::Task<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>> getGroupNodeInfo()
        override
    {
        co_return std::make_tuple(Error::Ptr(nullptr), bcos::gateway::GroupNodeInfo::Ptr(nullptr));
    }
    task::Task<Error::Ptr> onReceiveGroupNodeInfo(
        std::string, bcos::gateway::GroupNodeInfo::Ptr) override
    {
        co_return nullptr;
    }
    task::Task<Error::Ptr> onReceiveMessage(
        std::string groupID, bcos::crypto::NodeIDPtr nodeID, bytesConstRef data) override
    {
        received.emplace_back(std::move(groupID),
            std::string(reinterpret_cast<const char*>(data.data()), data.size()));
        co_return nullptr;
    }
    task::Task<Error::Ptr> onReceiveBroadcastMessage(
        std::string, bcos::crypto::NodeIDPtr, bytesConstRef) override
    {
        co_return nullptr;
    }
    task::Task<Error::Ptr> sendResponse(
        std::string, int, bcos::crypto::NodeIDPtr, bytesConstRef) override
    {
        co_return nullptr;
    }
    task::Task<void> broadcastMessage(
        uint16_t, int, ::ranges::any_view<bytesConstRef, ::ranges::category::forward>) override
    {
        co_return;
    }
    task::Task<bcos::front::SendResult> sendMessageByNodeID(int, bcos::crypto::NodeIDPtr,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward>, uint32_t) override
    {
        co_return bcos::front::SendResult{};
    }

    std::vector<std::pair<std::string, std::string>> received;
};

Message makeP2PRequest(uint32_t seq, bcos::crypto::NodeIDPtr const& dstNodeID)
{
    Message message;
    message.setPacketType(GatewayMessageType::PeerToPeerMessage);
    message.setSeq(seq);
    message.setPayload(bytes{'p'});
    P2PMessageOptions options;
    options.setGroupID("group0");
    options.setSrcNodeID(bytes(32, 0x1));
    options.mutableDstNodeIDs().push_back(dstNodeID->encode());
    message.setOptions(std::move(options));
    return message;
}

void closeP2PChannels(Service& service)
{
    for (uint16_t type : {GatewayMessageType::Handshake, GatewayMessageType::Heartbeat,
             GatewayMessageType::PeerToPeerMessage, GatewayMessageType::BroadcastMessage})
    {
        if (auto channel = service.inboundChannel(type))
        {
            channel->close();
        }
    }
}
}  // namespace

// Round-3 review (F1): the node-wide per-type PeerToPeerMessage pump must never await remote I/O.
// Peer A's response write is outstanding (parked in the fake above, exactly where production
// parks behind a stalled peer's write queue); a message from peer B must still reach the front
// service. The pre-fix handler co_awaited the response send on the pump, so B would have queued
// behind A until the inbound channel back-pressured.
BOOST_AUTO_TEST_CASE(p2pMessageDispatchNotBlockedByOutstandingResponseWrite)
{
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto service = std::make_shared<ParkedSendService>();
    auto nodeManager = std::make_shared<TestGatewayNodeManager>(keyFactory, service);
    auto front = std::make_shared<RecordingFrontService>();
    auto dstNodeID = keyFactory->createKey(bytes(32, 0x2));
    BOOST_REQUIRE(nodeManager->localRouterTable()->insertNode("group0", dstNodeID,
        bcos::protocol::NodeType::CONSENSUS_NODE, front,
        std::make_shared<ProtocolInfo>(
            ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1)));

    auto gateway = std::make_shared<Gateway>(nullptr, service, nodeManager, nullptr, nullptr);

    // No host is wired, so the channel poster wakes the pump inline: every step below completes
    // on this thread, deterministically.
    auto peerA = std::make_shared<FakePeerSession>("peerA");
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(1, dstNodeID), peerA));
    BOOST_REQUIRE_EQUAL(front->received.size(), 1u);  // A's dispatch ran
    BOOST_REQUIRE_EQUAL(service->sendStarted, 1);     // A's response write is outstanding
    BOOST_REQUIRE_EQUAL(service->sendReleased, 0);

    // B's message is dispatched while A's response write is STILL outstanding.
    auto peerB = std::make_shared<FakePeerSession>("peerB");
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(2, dstNodeID), peerB));
    BOOST_CHECK_EQUAL(front->received.size(), 2u);
    BOOST_CHECK_EQUAL(service->sendStarted, 2);
    BOOST_CHECK_EQUAL(service->sendReleased, 0);

    service->releaseAll();
    BOOST_CHECK_EQUAL(service->sendReleased, 2);
    closeP2PChannels(*service);
}

BOOST_AUTO_TEST_CASE(p2pMessageDispatchIsBoundedByInFlightBudget)
{
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto service = std::make_shared<ParkedSendService>();
    auto nodeManager = std::make_shared<TestGatewayNodeManager>(keyFactory, service);
    auto front = std::make_shared<RecordingFrontService>();
    auto dstNodeID = keyFactory->createKey(bytes(32, 0x2));
    BOOST_REQUIRE(nodeManager->localRouterTable()->insertNode("group0", dstNodeID,
        bcos::protocol::NodeType::CONSENSUS_NODE, front,
        std::make_shared<ProtocolInfo>(
            ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1)));
    const auto messageBytes = makeP2PRequest(0, dstNodeID).ownedStorageBytes();
    auto gateway = std::make_shared<Gateway>(nullptr, service, nodeManager, nullptr, nullptr,
        "localGateway", /*_dispatchInFlightBytes=*/2 * messageBytes,
        /*_dispatchInFlightCount=*/8);

    auto peerA = std::make_shared<FakePeerSession>("peerA");
    auto peerB = std::make_shared<FakePeerSession>("peerB");
    auto peerC = std::make_shared<FakePeerSession>("peerC");
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(1, dstNodeID), peerA));
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(2, dstNodeID), peerB));
    BOOST_REQUIRE_EQUAL(front->received.size(), 2u);
    BOOST_REQUIRE_EQUAL(service->sendStarted, 2);

    // Two requests fill the byte budget. The third is popped by the pump but cannot detach until
    // an earlier dispatch releases its permit.
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(3, dstNodeID), peerC));
    BOOST_CHECK_EQUAL(front->received.size(), 2u);
    BOOST_CHECK_EQUAL(service->sendStarted, 2);
    BOOST_CHECK_EQUAL(service->sendReleased, 0);

    service->releaseOne(0);
    BOOST_REQUIRE_EQUAL(service->sendReleased, 1);
    BOOST_REQUIRE_EQUAL(front->received.size(), 3u);
    BOOST_REQUIRE_EQUAL(service->sendStarted, 3);
    service->releaseAll();
    BOOST_CHECK_EQUAL(service->sendReleased, 3);
    closeP2PChannels(*service);
}

BOOST_AUTO_TEST_CASE(p2pMessageDispatchBudgetWaitIsCancelledByStop)
{
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto service = std::make_shared<ParkedSendService>();
    auto nodeManager = std::make_shared<TestGatewayNodeManager>(keyFactory, service);
    auto front = std::make_shared<RecordingFrontService>();
    auto dstNodeID = keyFactory->createKey(bytes(32, 0x2));
    BOOST_REQUIRE(nodeManager->localRouterTable()->insertNode("group0", dstNodeID,
        bcos::protocol::NodeType::CONSENSUS_NODE, front,
        std::make_shared<ProtocolInfo>(
            ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1)));
    std::vector<std::function<void()>> dispatchWakes;
    const auto messageBytes = makeP2PRequest(0, dstNodeID).ownedStorageBytes();
    auto gateway = std::make_shared<Gateway>(nullptr, service, nodeManager, nullptr, nullptr,
        "localGateway", /*_dispatchInFlightBytes=*/messageBytes, /*_dispatchInFlightCount=*/8,
        [&dispatchWakes](std::function<void()> wake) { dispatchWakes.push_back(std::move(wake)); });

    auto peerA = std::make_shared<FakePeerSession>("peerA");
    auto peerB = std::make_shared<FakePeerSession>("peerB");
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(1, dstNodeID), peerA));
    task::syncWait(service->onMessage(
        bcos::network::NetworkException{}, nullptr, makeP2PRequest(2, dstNodeID), peerB));
    BOOST_REQUIRE_EQUAL(front->received.size(), 1u);
    BOOST_REQUIRE_EQUAL(service->sendStarted, 1);

    std::weak_ptr<Gateway> weakGateway = gateway;
    gateway->stop();
    BOOST_REQUIRE_EQUAL(dispatchWakes.size(), 1);
    gateway.reset();
    service->releaseAll();
    BOOST_REQUIRE(weakGateway.expired());
    BOOST_CHECK_EQUAL(service->sendReleased, 1);

    // Resume the cancelled pump only after its Gateway owner is gone. It must exit through the
    // weak owner instead of touching the destroyed object.
    for (auto& wake : dispatchWakes)
    {
        wake();
    }
    BOOST_CHECK_EQUAL(front->received.size(), 1u);
    BOOST_CHECK_EQUAL(service->sendStarted, 1);
    closeP2PChannels(*service);
}

BOOST_AUTO_TEST_SUITE_END()
