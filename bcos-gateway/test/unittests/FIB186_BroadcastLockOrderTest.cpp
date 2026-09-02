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
#include "bcos-gateway/libp2p/P2PMessage.h"
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

    // Put one session in m_sessions so broadcastMessageToAll's loop actually sends once; capture
    // x_sessions for the probe thread.
    void arm()
    {
        m_xSessionsPtr = &x_sessions;
        m_sessions.emplace("peerRawP2pID", std::make_shared<P2PSession>());
    }

    // broadcastMessageToAll sends through the coroutine fast path (sendMessageByNodeID) for
    // each session, after snapshotting the session keys under x_sessions and releasing it. This
    // virtual is called for each session; a probe thread tries to take x_sessions EXCLUSIVELY: it
    // fails iff the broadcasting thread still holds it. No throw needed -- broadcastMessageToAll
    // touches no host; we just probe the lock state and stop.
    task::Task<Message::Ptr> sendMessageByNodeID(P2pID /*nodeID*/, P2PMessage& /*header*/,
        ::ranges::any_view<bytesConstRef> /*payloads*/, Options /*options*/) override
    {
        bool acquiredExclusive = false;
        std::thread probe([this, &acquiredExclusive]() {
            // try_lock() is permitted to fail spuriously; retry a bounded number of times so a
            // spurious failure is not misread as "asyncBroadcastMessage holds x_sessions". If it is
            // genuinely held (shared, by the broadcasting thread) every exclusive attempt fails; if
            // it is free the first attempt succeeds.
            for (int i = 0; i < 64 && !acquiredExclusive; ++i)
            {
                acquiredExclusive = m_xSessionsPtr->try_lock();
            }
            if (acquiredExclusive)
            {
                m_xSessionsPtr->unlock();
            }
        });
        probe.join();
        m_xSessionsHeldDuringSend = !acquiredExclusive;
        m_sendInvoked = true;
        co_return nullptr;
    }

    std::shared_mutex* m_xSessionsPtr = nullptr;
    std::atomic<bool> m_xSessionsHeldDuringSend{false};
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

    auto message = std::make_shared<P2PMessage>();
    task::wait([](std::shared_ptr<BroadcastProbeService> _service, P2PMessage::Ptr _message)
                   -> task::Task<void> {
        co_await _service->broadcastMessageToAll(
            _message, ::ranges::views::single(_message->payload()), Options{});
    }(service, message));

    BOOST_REQUIRE(service->m_sendInvoked.load());
    BOOST_CHECK_MESSAGE(!service->m_xSessionsHeldDuringSend.load(),
        "FIB-186 vector D: broadcastMessageToAll held x_sessions(shared) while sending to a "
        "session via sendMessageByNodeID (which re-acquires x_sessions via getP2PSessionByNodeId). "
        "That recursive shared lock deadlocks against a waiting onConnect writer and halts "
        "consensus. broadcastMessageToAll must snapshot the session keys under x_sessions, "
        "release it, then send.");
}

BOOST_AUTO_TEST_SUITE_END()
