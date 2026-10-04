/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file ServiceSubscribeTest.cpp
 * @brief Tests for Service::subscribe — the pull-mode inbound interface. The receive pump's
 *        dispatch prefers a subscribed channel over the legacy registerHandlerByMsgType
 *        callback, a full channel drops only that message, and unsubscribe falls back.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ServiceSubscribeTest, TestPromptFixture)

namespace
{
// Minimal P2PSession fake: onMessage needs a lockable weak_ptr<P2PSession> and a p2pID for
// logging; no socket or network is involved in any of these tests.
class FakeSubscribeSession : public P2PSession
{
public:
    explicit FakeSubscribeSession(std::string id) : m_id(std::move(id))
    {
        auto info = mutableP2pInfo();
        info->rawP2pID = m_id;
        info->p2pID = m_id;
    }
    bcos::network::P2pID p2pID() override { return m_id; }
    std::string printP2pID() override { return m_id; }
    std::string m_id;
};

Service::Ptr makeService()
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    return std::make_shared<Service>(selfInfo);
}

// Deliver one message of the given type through the pump's dispatch entry point, exactly as
// receiveLoop does after decode.
void deliver(Service& service, P2PSession::Ptr const& p2pSession, uint16_t packetType,
    std::string payload)
{
    Message message;
    message.setPacketType(packetType);
    message.setSeq(1);
    message.setPayload(bytes(payload.begin(), payload.end()));
    service.onMessage(bcos::network::NetworkException{}, nullptr, std::move(message), p2pSession);
}

// Inline poster: the tests are single-threaded, so resuming the consumer on the pusher's stack
// is exactly what we want.
InboundChannel::Poster inlinePoster()
{
    return [](std::function<void()> wake) { wake(); };
}

constexpr uint16_t TYPE_A = GatewayMessageType::PeerToPeerMessage;
constexpr uint16_t TYPE_B = GatewayMessageType::BroadcastMessage;
}  // namespace

BOOST_AUTO_TEST_CASE(subscribedChannelReceivesDispatchedMessage)
{
    auto service = makeService();
    auto channel = service->subscribe(TYPE_A, inlinePoster());
    auto peer = std::make_shared<FakeSubscribeSession>("peerA");

    deliver(*service, peer, TYPE_A, "hello");

    auto inbound = task::syncWait(channel->recv());
    BOOST_CHECK_EQUAL(inbound.session, peer);
    BOOST_CHECK_EQUAL(std::string(inbound.message.payload().begin(), inbound.message.payload().end()),
        "hello");
}

BOOST_AUTO_TEST_CASE(legacyHandlerUsedWhenNoChannel)
{
    auto service = makeService();
    auto peer = std::make_shared<FakeSubscribeSession>("peerB");

    std::string received;
    BOOST_REQUIRE(service->registerHandlerByMsgType(TYPE_B,
        [&received](bcos::network::NetworkException, std::shared_ptr<P2PSession>, Message message) {
            received.assign(message.payload().begin(), message.payload().end());
        }));

    deliver(*service, peer, TYPE_B, "legacy");
    BOOST_CHECK_EQUAL(received, "legacy");
}

BOOST_AUTO_TEST_CASE(channelShadowsLegacyHandlerForSameType)
{
    auto service = makeService();
    auto peer = std::make_shared<FakeSubscribeSession>("peerC");

    bool legacyCalled = false;
    BOOST_REQUIRE(service->registerHandlerByMsgType(
        TYPE_A, [&legacyCalled](bcos::network::NetworkException, std::shared_ptr<P2PSession>, Message) {
            legacyCalled = true;
        }));
    auto channel = service->subscribe(TYPE_A, inlinePoster());

    deliver(*service, peer, TYPE_A, "shadowed");

    BOOST_CHECK(!legacyCalled);
    auto inbound = task::syncWait(channel->recv());
    BOOST_CHECK_EQUAL(std::string(inbound.message.payload().begin(), inbound.message.payload().end()),
        "shadowed");
}

BOOST_AUTO_TEST_CASE(fullChannelDropsOnlyThatMessageAndServiceSurvives)
{
    auto service = makeService();
    auto channel = service->subscribe(TYPE_A, inlinePoster(), /*capacity=*/1);
    auto peer = std::make_shared<FakeSubscribeSession>("peerD");

    deliver(*service, peer, TYPE_A, "first");
    deliver(*service, peer, TYPE_A, "second");  // queue full: dropped, logged, no throw
    deliver(*service, peer, TYPE_A, "third");   // still full: same

    auto inbound = task::syncWait(channel->recv());
    BOOST_CHECK_EQUAL(std::string(inbound.message.payload().begin(), inbound.message.payload().end()),
        "first");
    // both overflow messages were dropped; the queue is now empty
    BOOST_CHECK_EQUAL(channel->size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
