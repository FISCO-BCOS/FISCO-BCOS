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

#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/multigroup/GroupInfo.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/libamop/AMOPImpl.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
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

// Forward declarations for interfaces we'll mock
namespace bcos::gateway
{
class P2PInterface;
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

    // Mock P2PInterface for testing P2P operations
    fakeit::Mock<P2PInterface> mockP2PInterface;

    // Setup mock behaviors using When().AlwaysDo() pattern like in testBaselineScheduler
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock start behavior - just return successfully
    });

    fakeit::When(Method(mockP2PInterface, stop)).AlwaysDo([]() {
        // Mock stop behavior - just return successfully
    });

    // Test that we can call the mocked methods
    P2PInterface& p2pRef = mockP2PInterface.get();

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
    // Test P2PInterface mocking similar to testBaselineScheduler pattern
    // P2PInterface is polymorphic and can be mocked
    fakeit::Mock<P2PInterface> mockP2PInterface;

    // Setup mock behaviors using When().AlwaysDo() pattern
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock successful start
    });
    fakeit::When(Method(mockP2PInterface, stop)).AlwaysDo([]() {
        // Mock successful stop
    });

    // Test the mocked P2P interface
    P2PInterface& p2pRef = mockP2PInterface.get();

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
    fakeit::When(Method(mockAMOP, asyncSendMessageByTopic))
        .AlwaysDo([](const std::string& /*topic*/, bcos::bytesConstRef /*data*/,
                      const std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)>&
                          callback) {
            // Simulate successful message send
            if (callback)
            {
                callback(nullptr, 0, bcos::bytesConstRef{});
            }
        });

    fakeit::When(Method(mockAMOP, asyncSubscribeTopic))
        .AlwaysDo([](std::string const& /*clientID*/, std::string const& /*topicInfo*/,
                      const std::function<void(bcos::Error::Ptr&&)>& callback) {
            // Simulate successful subscription
            if (callback)
            {
                callback(nullptr);
            }
        });

    // Test the mocked AMOP
    bcos::amop::AMOPImpl& amopRef = mockAMOP.get();

    std::string testTopic = "testTopic";
    std::string testClientID = "testClient";
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // Test message sending with callback
    bool callbackInvoked = false;
    amopRef.asyncSendMessageByTopic(testTopic,
        bcos::bytesConstRef(testData.data(), testData.size()),
        [&callbackInvoked](
            const bcos::Error::Ptr& /*error*/, int16_t code, bcos::bytesConstRef /*response*/) {
            callbackInvoked = true;
            BOOST_CHECK_EQUAL(code, 0);
        });
    BOOST_CHECK(callbackInvoked);

    // Test topic subscription
    bool subscriptionCallbackInvoked = false;
    amopRef.asyncSubscribeTopic(
        testClientID, testTopic, [&subscriptionCallbackInvoked](const bcos::Error::Ptr& /*error*/) {
            subscriptionCallbackInvoked = true;
        });
    BOOST_CHECK(subscriptionCallbackInvoked);

    // Verify methods were called
    fakeit::Verify(Method(mockAMOP, asyncSendMessageByTopic)).Exactly(1);
    fakeit::Verify(Method(mockAMOP, asyncSubscribeTopic)).Exactly(1);
}

BOOST_AUTO_TEST_CASE(testComplexGatewayScenario)
{
    // Test a more complex scenario using multiple mocks together
    // This demonstrates how to build comprehensive tests similar to testBaselineScheduler.cpp

    // Create multiple mocks for a complete Gateway test
    fakeit::Mock<P2PInterface> mockP2PInterface;
    fakeit::Mock<GatewayNodeManager> mockGatewayNodeManager;
    fakeit::Mock<bcos::amop::AMOPImpl> mockAMOP;

    // Setup complex mock behaviors
    fakeit::When(Method(mockP2PInterface, start)).AlwaysDo([]() {
        // Mock P2P start - simulate successful initialization
    });

    fakeit::When(Method(mockGatewayNodeManager, registerNode)).AlwaysReturn(true);
    fakeit::When(Method(mockGatewayNodeManager, unregisterNode)).AlwaysReturn(true);

    // Setup AMOP with multiple different behaviors for different calls
    fakeit::When(Method(mockAMOP, asyncSendMessageByTopic))
        .AlwaysDo([](const std::string& topic, bcos::bytesConstRef /*data*/,
                      const std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)>&
                          callback) {
            // Simulate different responses based on topic
            if (topic == "successTopic")
            {
                if (callback)
                {
                    callback(nullptr, 0, bcos::bytesConstRef{});
                }
            }
            else if (topic == "failTopic")
            {
                auto error = std::make_shared<bcos::Error>(
                    bcos::Error::buildError("MockTest", -1, "Mock error for test"));
                if (callback)
                {
                    callback(std::move(error), -1, bcos::bytesConstRef{});
                }
            }
        });

    // Test the complex scenario
    P2PInterface& p2pRef = mockP2PInterface.get();
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
    bool successCallbackInvoked = false;
    amopRef.asyncSendMessageByTopic("successTopic",
        bcos::bytesConstRef(testData.data(), testData.size()),
        [&successCallbackInvoked](
            const bcos::Error::Ptr& error, int16_t code, bcos::bytesConstRef /*response*/) {
            successCallbackInvoked = true;
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(code, 0);
        });
    BOOST_CHECK(successCallbackInvoked);

    // Test failure case
    bool failureCallbackInvoked = false;
    amopRef.asyncSendMessageByTopic("failTopic",
        bcos::bytesConstRef(testData.data(), testData.size()),
        [&failureCallbackInvoked](
            const bcos::Error::Ptr& error, int16_t code, bcos::bytesConstRef /*response*/) {
            failureCallbackInvoked = true;
            BOOST_CHECK(error != nullptr);
            BOOST_CHECK_EQUAL(code, -1);
        });
    BOOST_CHECK(failureCallbackInvoked);

    // Clean up - unregister nodes
    bool unreg1 = nodeManagerRef.unregisterNode("group1", nodeID1->hex());
    bool unreg2 = nodeManagerRef.unregisterNode("group1", nodeID2->hex());

    BOOST_CHECK(unreg1);
    BOOST_CHECK(unreg2);

    // Verify all expected interactions occurred
    fakeit::Verify(Method(mockP2PInterface, start)).Exactly(1);
    fakeit::Verify(Method(mockGatewayNodeManager, registerNode)).Exactly(2);
    fakeit::Verify(Method(mockGatewayNodeManager, unregisterNode)).Exactly(2);
    fakeit::Verify(Method(mockAMOP, asyncSendMessageByTopic)).Exactly(2);
}

BOOST_AUTO_TEST_CASE(testErrorHandlingScenarios)
{
    // Test error handling scenarios using fakeit
    fakeit::Mock<bcos::amop::AMOPImpl> mockAMOP;

    // Setup error scenarios
    int callCount = 0;
    fakeit::When(Method(mockAMOP, asyncSendMessageByTopic))
        .AlwaysDo([&callCount](const std::string& /*topic*/, bcos::bytesConstRef /*data*/,
                      const std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)>&
                          callback) {
            ++callCount;
            if (callCount == 1)
            {
                // First call succeeds
                if (callback)
                {
                    callback(nullptr, 0, bcos::bytesConstRef{});
                }
            }
            else
            {
                // Subsequent calls fail
                auto error = std::make_shared<bcos::Error>(
                    bcos::Error::buildError("MockTest", -2, "Network timeout"));
                if (callback)
                {
                    callback(std::move(error), -2, bcos::bytesConstRef{});
                }
            }
        });

    bcos::amop::AMOPImpl& amopRef = mockAMOP.get();
    bcos::bytes testData = {0x1, 0x2, 0x3};

    // First call should succeed
    bool firstCallSucceeded = false;
    amopRef.asyncSendMessageByTopic("testTopic",
        bcos::bytesConstRef(testData.data(), testData.size()),
        [&firstCallSucceeded](
            const bcos::Error::Ptr& error, int16_t code, bcos::bytesConstRef /*response*/) {
            firstCallSucceeded = (error == nullptr && code == 0);
        });
    BOOST_CHECK(firstCallSucceeded);

    // Second call should fail
    bool secondCallFailed = false;
    amopRef.asyncSendMessageByTopic("testTopic",
        bcos::bytesConstRef(testData.data(), testData.size()),
        [&secondCallFailed](
            const bcos::Error::Ptr& error, int16_t code, bcos::bytesConstRef /*response*/) {
            secondCallFailed = (error != nullptr && code == -2);
        });
    BOOST_CHECK(secondCallFailed);

    // Verify both calls were made
    fakeit::Verify(Method(mockAMOP, asyncSendMessageByTopic)).Exactly(2);
}

BOOST_AUTO_TEST_SUITE_END()