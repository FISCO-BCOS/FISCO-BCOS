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
 *        dispatch goes through the channel subscribed for the message type, a full channel parks
 *        the pushing pump (back-pressure, no drop), and subscribeHandler() runs a coroutine
 *        handler per message.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

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
// receiveLoop does after decode. onMessage is a coroutine since the per-type channel
// back-pressures; the tests below never fill a channel past capacity, so this completes inline.
void deliver(
    Service& service, P2PSession::Ptr const& p2pSession, uint16_t packetType, std::string payload)
{
    Message message;
    message.setPacketType(packetType);
    message.setSeq(1);
    message.setPayload(bytes(payload.begin(), payload.end()));
    task::syncWait(service.onMessage(
        bcos::network::NetworkException{}, nullptr, std::move(message), p2pSession));
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
    BOOST_CHECK_EQUAL(
        std::string(inbound.message.payload().begin(), inbound.message.payload().end()), "hello");
}

// The handler-pump path: subscribeHandler() runs a coroutine handler per inbound message, in
// arrival order. (The old push-callback API — registerHandlerByMsgType — is gone; every type is
// dispatched through its channel.)
BOOST_AUTO_TEST_CASE(handlerPumpReceivesDispatchedMessagesInOrder)
{
    auto service = makeService();
    auto peer = std::make_shared<FakeSubscribeSession>("peerB");

    std::vector<std::string> received;
    service->subscribeHandler(
        TYPE_B,
        [&received](bcos::gateway::InboundMessage&& inbound) -> task::Task<bool> {
            received.emplace_back(
                inbound.message.payload().begin(), inbound.message.payload().end());
            co_return true;
        },
        /*capacity=*/8);

    deliver(*service, peer, TYPE_B, "one");
    deliver(*service, peer, TYPE_B, "two");

    // subscribeHandler's pump runs on the SHARED POOL (unlike the inline-poster channel tests
    // above), so the drain happens on another thread and this test has to wait for it.
    for (int i = 0; i < 500 && received.size() < 2; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    BOOST_REQUIRE_EQUAL(received.size(), 2u);
    BOOST_CHECK_EQUAL(received[0], "one");
    BOOST_CHECK_EQUAL(received[1], "two");
}

// A handler that returns false stops its own pump; later messages pile into the channel instead of
// running the handler.
BOOST_AUTO_TEST_CASE(handlerCanStopItsPump)
{
    auto service = makeService();
    auto peer = std::make_shared<FakeSubscribeSession>("peerC");

    int calls = 0;
    service->subscribeHandler(
        TYPE_B,
        [&calls](bcos::gateway::InboundMessage&&) -> task::Task<bool> {
            ++calls;
            co_return false;  // stop after the first message
        },
        /*capacity=*/8);

    deliver(*service, peer, TYPE_B, "first");
    deliver(*service, peer, TYPE_B, "second");

    // The pump stops itself after the first message; wait for that to have happened, then give the
    // second message time to NOT be handled.
    for (int i = 0; i < 500 && calls < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    BOOST_CHECK_EQUAL(calls, 1);
}

// A full channel no longer drops the message (round-3 review): the pushing onMessage parks in
// waitWritable() until the consumer drains space, and the parked message survives to be delivered.
BOOST_AUTO_TEST_CASE(fullChannelParksThePushInsteadOfDropping)
{
    auto service = makeService();
    auto channel = service->subscribe(TYPE_A, inlinePoster(), /*capacity=*/1);
    auto peer = std::make_shared<FakeSubscribeSession>("peerD");

    deliver(*service, peer, TYPE_A, "first");
    BOOST_REQUIRE_EQUAL(channel->size(), 1);

    // The channel is full: the next onMessage must PARK, not drop. Drive it detached and record
    // completion; with the inline poster every step runs on this thread, deterministically.
    bool delivered = false;
    task::wait(
        [](Service& service, P2PSession::Ptr p2pSession, bool& deliveredFlag) -> task::Task<void> {
            Message message;
            message.setPacketType(TYPE_A);
            message.setSeq(2);
            message.setPayload(bytes{'x'});
            co_await service.onMessage(bcos::network::NetworkException{}, nullptr,
                std::move(message), std::move(p2pSession));
            deliveredFlag = true;
        }(*service, peer, delivered));
    BOOST_CHECK(!delivered);  // still parked: no drop, no overwrite
    BOOST_CHECK_EQUAL(channel->size(), 1);

    // Draining one slot frees the space the parked producer was waiting for; its push then
    // completes inline (the pop wakes it through the poster).
    auto first = task::syncWait(channel->recv());
    BOOST_CHECK_EQUAL(
        std::string(first.message.payload().begin(), first.message.payload().end()), "first");
    BOOST_REQUIRE(delivered);
    BOOST_CHECK_EQUAL(channel->size(), 1);
    auto second = task::syncWait(channel->recv());
    BOOST_CHECK_EQUAL(
        std::string(second.message.payload().begin(), second.message.payload().end()), "x");
}

BOOST_AUTO_TEST_SUITE_END()
