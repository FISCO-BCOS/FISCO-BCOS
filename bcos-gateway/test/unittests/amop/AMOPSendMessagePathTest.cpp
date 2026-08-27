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
 * @brief test for AMOPImpl::asyncSendMessageByTopic retry path
 * @file AMOPSendMessagePathTest.cpp
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/AMOPRequest.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-gateway/libamop/AMOPImpl.h"
#include "bcos-gateway/libamop/AMOPMessage.h"
#include "bcos-gateway/libamop/TopicManager.h"
#include "bcos-gateway/libp2p/P2PInterface.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"

#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace fakeit;
using namespace bcos;
using namespace bcos::test;
using namespace bcos::amop;
using namespace bcos::gateway;
using namespace bcos::protocol;

namespace
{
// encode an AMOPMessage the same way a real peer would
bcos::bytes encodeAMOPResponse(uint16_t _status, std::string const& _data)
{
    AMOPMessage amopMsg;
    amopMsg.setType(AMOPMessage::Type::AMOPResponse);
    amopMsg.setStatus(_status);
    amopMsg.setData(bytesConstRef((bcos::byte*)_data.data(), _data.size()));
    bcos::bytes payload;
    amopMsg.encode(payload);
    return payload;
}

Message::Ptr buildP2PResponse(
    bcos::bytes _payload, uint16_t _packetType = GatewayMessageType::AMOPMessageType)
{
    auto message = std::make_shared<P2PMessageV2>();
    message->setPacketType(_packetType);
    message->setPayload(std::move(_payload));
    return message;
}

struct SendResult
{
    bool called = false;
    bcos::Error::Ptr error;
    int16_t packetType = 0;
    bcos::bytes responseData;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(AMOPSendMessagePathTest, TestPromptFixture)

struct AMOPSendFixture
{
    AMOPSendFixture()
    {
        // AMOPImpl registers an AMOP message handler in its constructor
        When(Method(networkMock, registerHandlerByMsgType)).AlwaysReturn(true);
        // TopicManager::queryNodeIDsByTopic only returns reachable nodes
        When(Method(networkMock, isReachable)).AlwaysReturn(true);
        When(Method(networkMock, messageFactory)).AlwaysDo([]() -> std::shared_ptr<MessageFactory> {
            return std::make_shared<P2PMessageFactoryV2>();
        });

        network = P2PInterface::Ptr(&networkMock.get(), [](P2PInterface*) {});
        topicManager = std::make_shared<TopicManager>("amopSendPathTest", network);
        ioServicePool = std::make_shared<bcos::IOServicePool>(1, "amopSendPathTest");
        amop = std::make_shared<AMOPImpl>(topicManager, std::make_shared<AMOPMessageFactory>(),
            std::make_shared<bcos::protocol::AMOPRequestFactory>(), network, localNodeID,
            ioContext, ioServicePool);
    }

    void subscribeTopic(std::string const& _topic, std::vector<P2pID> const& _nodeIDs)
    {
        TopicItems topicItems;
        topicItems.insert(TopicItem(_topic));
        for (size_t i = 0; i < _nodeIDs.size(); ++i)
        {
            topicManager->updateSeqAndTopicsByNodeID(_nodeIDs[i], i + 1, topicItems);
        }
    }

    void send(std::string const& _topic, SendResult& _result)
    {
        bcos::bytes data = {'h', 'e', 'l', 'l', 'o'};
        amop->asyncSendMessageByTopic(_topic, ref(data),
            [&_result](bcos::Error::Ptr&& _error, int16_t _type, bytesConstRef _responseData) {
                _result.called = true;
                _result.error = std::move(_error);
                _result.packetType = _type;
                _result.responseData = bcos::bytes(_responseData.begin(), _responseData.end());
            });
    }

    Mock<P2PInterface> networkMock;
    boost::asio::io_context ioContext;
    bcos::IOServicePool::Ptr ioServicePool;
    P2PInterface::Ptr network;
    TopicManager::Ptr topicManager;
    std::shared_ptr<AMOPImpl> amop;
    P2pID localNodeID = std::string(128, 'f');
    std::shared_ptr<std::vector<P2pID>> attempts = std::make_shared<std::vector<P2pID>>();
};

// no node subscribes the topic: fail fast, never touch the network
BOOST_AUTO_TEST_CASE(test_emptyCandidateList)
{
    AMOPSendFixture fixture;

    SendResult result;
    fixture.send("topic_without_subscriber", result);

    BOOST_CHECK(result.called);
    BOOST_CHECK(result.error != nullptr);
    BOOST_CHECK_EQUAL(result.error->errorCode(), CommonError::NotFoundPeerByTopicSendMsg);
    BOOST_CHECK_EQUAL(result.packetType, 0);
    BOOST_CHECK(fixture.attempts->empty());
    Verify(Method(fixture.networkMock, sendMessageByNodeID)).Never();
}

// every candidate throws NetworkException: all are tried once, terminal AMOPSendMsgFailed
BOOST_AUTO_TEST_CASE(test_allCandidatesFail)
{
    AMOPSendFixture fixture;
    std::vector<P2pID> nodeIDs = {std::string(128, 'a'), std::string(128, 'b'),
        std::string(128, 'c')};
    fixture.subscribeTopic("topic_all_fail", nodeIDs);

    auto attempts = fixture.attempts;
    When(Method(fixture.networkMock, sendMessageByNodeID))
        .AlwaysDo([attempts](P2pID nodeID, P2PMessage&, ::ranges::any_view<bytesConstRef>,
                      Options) -> task::Task<Message::Ptr> {
            attempts->push_back(nodeID);
            if (!nodeID.empty())
            {
                throw NetworkException(-1, "mock network failure");
            }
            co_return Message::Ptr{};
        });

    SendResult result;
    fixture.send("topic_all_fail", result);

    // each candidate tried exactly once (order is shuffled, so compare as sets)
    BOOST_CHECK_EQUAL(attempts->size(), nodeIDs.size());
    auto sortedAttempts = *attempts;
    auto sortedNodeIDs = nodeIDs;
    std::sort(sortedAttempts.begin(), sortedAttempts.end());
    std::sort(sortedNodeIDs.begin(), sortedNodeIDs.end());
    BOOST_CHECK(sortedAttempts == sortedNodeIDs);

    BOOST_CHECK(result.called);
    BOOST_CHECK(result.error != nullptr);
    BOOST_CHECK_EQUAL(result.error->errorCode(), CommonError::AMOPSendMsgFailed);
    BOOST_CHECK_EQUAL(result.packetType, 0);
    BOOST_CHECK(result.responseData.empty());
}

// first attempt throws, second succeeds: retry kicks in and the third node is left untried.
// The candidate list is shuffled, so behavior is keyed on the attempt count, not the node order.
BOOST_AUTO_TEST_CASE(test_retrySucceedsAfterNetworkException)
{
    AMOPSendFixture fixture;
    std::vector<P2pID> nodeIDs = {std::string(128, 'a'), std::string(128, 'b'),
        std::string(128, 'c')};
    fixture.subscribeTopic("topic_retry_success", nodeIDs);

    const uint16_t c_responseStatus = 7;
    const std::string c_responseData = "mock amop response";
    auto expectedPayload = encodeAMOPResponse(c_responseStatus, c_responseData);

    auto attempts = fixture.attempts;
    When(Method(fixture.networkMock, sendMessageByNodeID))
        .AlwaysDo([attempts, expectedPayload](P2pID nodeID, P2PMessage&,
                      ::ranges::any_view<bytesConstRef>, Options) -> task::Task<Message::Ptr> {
            attempts->push_back(nodeID);
            if (attempts->size() == 1)
            {
                throw NetworkException(-1, "mock network failure");
            }
            co_return buildP2PResponse(expectedPayload);
        });

    SendResult result;
    fixture.send("topic_retry_success", result);

    // two distinct candidates tried, the third one left untried
    BOOST_CHECK_EQUAL(attempts->size(), 2);
    BOOST_CHECK((*attempts)[0] != (*attempts)[1]);
    for (auto const& nodeID : *attempts)
    {
        BOOST_CHECK(std::find(nodeIDs.begin(), nodeIDs.end(), nodeID) != nodeIDs.end());
    }

    BOOST_CHECK(result.called);
    BOOST_CHECK(result.error != nullptr);
    BOOST_CHECK_EQUAL(result.error->errorCode(), c_responseStatus);
    BOOST_CHECK_EQUAL(result.packetType, GatewayMessageType::AMOPMessageType);
    BOOST_CHECK(result.responseData == expectedPayload);
}

// a null response is a (retryable) send failure, not a silent success
BOOST_AUTO_TEST_CASE(test_nullResponseRetriesNextNode)
{
    AMOPSendFixture fixture;
    std::vector<P2pID> nodeIDs = {std::string(128, 'a'), std::string(128, 'b'),
        std::string(128, 'c')};
    fixture.subscribeTopic("topic_null_response", nodeIDs);

    auto expectedPayload = encodeAMOPResponse(0, "ok");
    auto attempts = fixture.attempts;
    When(Method(fixture.networkMock, sendMessageByNodeID))
        .AlwaysDo([attempts, expectedPayload](P2pID nodeID, P2PMessage&,
                      ::ranges::any_view<bytesConstRef>, Options) -> task::Task<Message::Ptr> {
            attempts->push_back(nodeID);
            if (attempts->size() == 1)
            {
                co_return Message::Ptr{};
            }
            co_return buildP2PResponse(expectedPayload);
        });

    SendResult result;
    fixture.send("topic_null_response", result);

    BOOST_CHECK_EQUAL(attempts->size(), 2);
    BOOST_CHECK((*attempts)[0] != (*attempts)[1]);
    BOOST_CHECK(result.called);
    BOOST_CHECK(result.error != nullptr);
    BOOST_CHECK_EQUAL(result.error->errorCode(), 0);
    BOOST_CHECK_EQUAL(result.packetType, GatewayMessageType::AMOPMessageType);
    BOOST_CHECK(result.responseData == expectedPayload);
}

// an AMOP reply that cannot be decoded means the request was DELIVERED (the peer answered):
// the caller is failed, the payload is NOT replayed to another subscriber
BOOST_AUTO_TEST_CASE(test_malformedAMOPResponseFailsWithoutRetry)
{
    AMOPSendFixture fixture;
    std::vector<P2pID> nodeIDs = {std::string(128, 'a'), std::string(128, 'b'),
        std::string(128, 'c')};
    fixture.subscribeTopic("topic_malformed_response", nodeIDs);

    auto attempts = fixture.attempts;
    When(Method(fixture.networkMock, sendMessageByNodeID))
        .AlwaysDo([attempts](P2pID nodeID, P2PMessage&, ::ranges::any_view<bytesConstRef>,
                      Options) -> task::Task<Message::Ptr> {
            attempts->push_back(nodeID);
            // AMOPMessage::decode needs at least the 6-byte header
            co_return buildP2PResponse(bcos::bytes{0x1, 0x2, 0x3});
        });

    SendResult result;
    fixture.send("topic_malformed_response", result);

    // exactly one node is tried; the caller is failed without replaying the payload
    BOOST_CHECK_EQUAL(attempts->size(), 1);
    BOOST_CHECK(result.called);
    BOOST_CHECK(result.error != nullptr);
    BOOST_CHECK_EQUAL(result.error->errorCode(), CommonError::AMOPSendMsgFailed);
    BOOST_CHECK_EQUAL(result.packetType, GatewayMessageType::AMOPMessageType);
    BOOST_CHECK(result.responseData.empty());
}

// the retry loop must wait for responses with a finite timeout: Options{0, true} never times
// out (Session registers no timer for a zero timeout), which is the round-3 Finding A hang.
// Pin the contract so reverting to a zero timeout fails this test.
BOOST_AUTO_TEST_CASE(test_sendUsesFiniteResponseTimeout)
{
    AMOPSendFixture fixture;
    std::vector<P2pID> nodeIDs = {std::string(128, 'a')};
    fixture.subscribeTopic("topic_finite_timeout", nodeIDs);

    auto expectedPayload = encodeAMOPResponse(0, "ok");
    auto observedOptions = std::make_shared<std::vector<Options>>();
    When(Method(fixture.networkMock, sendMessageByNodeID))
        .AlwaysDo([observedOptions, expectedPayload](P2pID, P2PMessage&,
                      ::ranges::any_view<bytesConstRef>, Options options)
                      -> task::Task<Message::Ptr> {
            observedOptions->push_back(options);
            co_return buildP2PResponse(expectedPayload);
        });

    SendResult result;
    fixture.send("topic_finite_timeout", result);

    BOOST_CHECK(result.called);
    BOOST_REQUIRE_EQUAL(observedOptions->size(), 1);
    BOOST_CHECK((*observedOptions)[0].response);
    BOOST_CHECK_EQUAL((*observedOptions)[0].timeout, 30000);
}

BOOST_AUTO_TEST_SUITE_END()
