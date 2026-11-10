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
 * @brief Regression test for FIB-186 vector D: the recursive x_sessions shared-lock deadlock in
 *        Service::broadcastMessageToAll (previously Service::asyncBroadcastMessage).
 * @file FIB186_BroadcastLockOrderTest.cpp
 * @date 2026-07-21
 *
 * broadcastMessageToAll held x_sessions(shared) while looping over m_sessions and calling
 * sendMessageByNodeID(), which re-acquires x_sessions(shared) via getP2PSessionByNodeId. That
 * is a RECURSIVE shared lock. std::shared_mutex is not recursive under a waiting writer: as soon as
 * an onConnect thread is blocked on x_sessions(W), libc++ writer-priority blocks the second
 * lock_shared(), so the broadcasting thread can neither finish nor release its first shared lock
 * and the writer never proceeds -> all session operations (including every PBFT send/broadcast)
 * stall and consensus halts under connection churn. This was the deadlock that actually froze block
 * production in the live CertiK-injector reproduction (heartBeat stops, height frozen, no
 * recovery).
 *
 * The fix snapshots the session keys under x_sessions, releases it, then sends. This test drives
 * the real Service::broadcastMessageToAll and asserts x_sessions is not held while it calls
 * sendMessageByNodeID. It is RED on the pre-fix code and GREEN after.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <shared_mutex>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB186_BroadcastLockOrderTest, TestPromptFixture)

namespace
{
class BroadcastProbeService : public Service
{
public:
    explicit BroadcastProbeService(P2PInfo const& _info) : Service(_info) {}

    // Put one session in the table so broadcastMessageToAll's loop actually sends once.
    void arm()
    {
        m_sessions.update([&](SessionsType& table) {
            table.emplace("peerRawP2pID", std::make_shared<P2PSession>());
        });
    }

    // broadcastMessageToAll takes a snapshot of the session table and releases it before sending,
    // so a concurrent session-table WRITER (a connect/disconnect) must never be blocked by a
    // broadcast. Under the old shared_mutex this was the FIB-186 vector D deadlock: a writer
    // waiting for x_sessions while the broadcast held it shared. This probe runs a writer inside
    // the per-session send and asserts it completes.
    task::Task<std::optional<Message>> sendMessageByNodeID(P2pID /*nodeID*/, Message& /*header*/,
        ::ranges::any_view<bytesConstRef> /*payloads*/, Options /*options*/) override
    {
        std::atomic<bool> writerDone{false};
        std::thread probe([this, &writerDone]() {
            m_sessions.update([](SessionsType& table) {
                // a no-op mutation: the point is that the clone-publish does not wait on the reader
                (void)table.size();
            });
            writerDone.store(true);
        });
        probe.join();
        m_writerBlockedByBroadcast.store(!writerDone.load());
        m_sendInvoked.store(true);
        co_return std::nullopt;
    }

    std::atomic<bool> m_writerBlockedByBroadcast{false};
    std::atomic<bool> m_sendInvoked{false};
};
}  // namespace

BOOST_AUTO_TEST_CASE(BroadcastDoesNotHoldSessionsLockWhileSending)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<BroadcastProbeService>(selfInfo);
    service->arm();

    auto message = std::make_shared<Message>();
    task::wait([](std::shared_ptr<BroadcastProbeService> _service,
                   Message::Ptr _message) -> task::Task<void> {
        co_await _service->broadcastMessageToAll(
            _message, ::ranges::views::single(_message->payload()), Options{});
    }(service, message));

    BOOST_REQUIRE(service->m_sendInvoked.load());
    BOOST_CHECK_MESSAGE(!service->m_writerBlockedByBroadcast.load(),
        "FIB-186 vector D: broadcastMessageToAll held x_sessions(shared) while sending to a "
        "session via sendMessageByNodeID (which re-acquires x_sessions via getP2PSessionByNodeId). "
        "That recursive shared lock deadlocks against a waiting onConnect writer and halts "
        "consensus. broadcastMessageToAll must snapshot the session keys under x_sessions, "
        "release it, then send.");
}

BOOST_AUTO_TEST_SUITE_END()
