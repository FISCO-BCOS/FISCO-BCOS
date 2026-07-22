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
 * @brief Regression test for FIB-186 vector D: the x_nodes/x_sessions lock-order inversion between
 *        Service::heartBeat and Service::onConnect that deadlocks under connection churn.
 * @file FIB186_HeartBeatLockOrderTest.cpp
 * @date 2026-07-21
 *
 * The two P2P-service locks are taken in opposite orders on two hot paths:
 *   onConnect : x_sessions(W) [Service.cpp] -> updateStaticNodes -> x_nodes(W)
 *   heartBeat : x_nodes(R)                  -> isConnected        -> x_sessions(R)
 * When a bulk-disconnect flood drives onConnect's duplicate-peer branch at high frequency while the
 * heartBeat timer runs the opposite order, the two threads deadlock (onConnect holds x_sessions(W)
 * waiting for x_nodes(W); heartBeat holds x_nodes(R) waiting for x_sessions(R)) and consensus
 * halts.
 *
 * This test drives the REAL Service::heartBeat and checks the invariant the fix must satisfy:
 * heartBeat must not hold x_nodes while it calls isConnected() (which takes x_sessions). It is RED
 * on the pre-fix code (heartBeat holds x_nodes across the whole reconnect loop) and GREEN once
 * heartBeat snapshots m_staticNodes under x_nodes and releases it before the isConnected() work.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <shared_mutex>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB186_HeartBeatLockOrderTest, TestPromptFixture)

namespace
{
class ProbeService : public Service
{
public:
    struct StopHeartBeat
    {
    };

    explicit ProbeService(P2PInfo const& _info) : Service(_info) {}

    // Arm heartBeat: mark the service running and add one static node with a non-empty p2pID that
    // is not self, so heartBeat's reconnect loop actually calls isConnected() (the x_sessions
    // acquisition). Capture x_nodes so the probe thread below can test whether heartBeat holds it.
    void arm()
    {
        m_xNodesPtr = &x_nodes;
        m_run = true;
        m_staticNodes[NodeIPEndpoint("127.0.0.1", 30300)] = "peerRawP2pID";
    }

    // heartBeat calls this (virtual dispatch) from inside its reconnect loop. On the pre-fix code
    // heartBeat still holds x_nodes here. A separate probe thread tries to take x_nodes
    // EXCLUSIVELY: std::shared_mutex::try_lock() fails iff the mutex is held in any mode by any
    // thread, so it fails exactly when heartBeat still holds x_nodes(shared). Then throw to abort
    // heartBeat before its host-dependent tail (asioInterface()->newTimer()), which no test host
    // provides.
    bool isConnected(P2pID const& /*nodeID*/) const override
    {
        bool acquiredExclusive = false;
        std::thread probe([this, &acquiredExclusive]() {
            // try_lock() is permitted to fail spuriously; retry a bounded number of times so a
            // spurious failure is not misread as "heartBeat holds x_nodes". If it is genuinely
            // held (shared, by heartBeat) every exclusive attempt fails; if it is free the first
            // attempt succeeds.
            for (int i = 0; i < 64 && !acquiredExclusive; ++i)
            {
                acquiredExclusive = m_xNodesPtr->try_lock();
            }
            if (acquiredExclusive)
            {
                m_xNodesPtr->unlock();
            }
        });
        probe.join();
        m_xNodesHeldDuringIsConnected = !acquiredExclusive;
        m_isConnectedInvoked = true;
        throw StopHeartBeat{};
    }

    // Test hooks.
    size_t staticNodeCount() const { return m_staticNodes.size(); }
    void probeIsConnected() { isConnected("peerRawP2pID"); }
    // heartBeat is aborted mid-way (via the throw) so m_host is never set; clear m_run so
    // stop() does not dereference the null host.
    void disarm() { m_run = false; }

    // Exception-safety guard. arm() sets m_run=true without a host; Service::stop() runs its whole
    // body (including m_host->stop()) whenever m_run is true. If any BOOST_REQUIRE below throws
    // before the explicit disarm(), the fixture unwinds and ~Service()->stop() would dereference
    // the null m_host and SIGSEGV the whole test binary with no report. Resetting m_run here makes
    // teardown safe regardless of where a check throws.
    ~ProbeService() override { m_run = false; }

    std::shared_mutex* m_xNodesPtr = nullptr;
    mutable std::atomic<bool> m_xNodesHeldDuringIsConnected{false};
    mutable std::atomic<bool> m_isConnectedInvoked{false};
};
}  // namespace

BOOST_AUTO_TEST_CASE(HeartBeatDoesNotHoldNodesLockWhileTakingSessionsLock)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<ProbeService>(selfInfo);
    service->arm();

    // Diagnostics: the static-node list must hold exactly the armed peer, and the isConnected
    // override must dispatch (throw) when called directly -- so a crash below is unambiguous.
    BOOST_REQUIRE_EQUAL(service->staticNodeCount(), 1U);
    BOOST_REQUIRE_THROW(service->probeIsConnected(), ProbeService::StopHeartBeat);
    service->m_isConnectedInvoked = false;

    bool aborted = false;
    try
    {
        service->heartBeat();
    }
    catch (ProbeService::StopHeartBeat const&)
    {
        aborted = true;
    }

    // The probe must have actually run, otherwise the assertion below would be vacuous.
    BOOST_REQUIRE(service->m_isConnectedInvoked.load());
    BOOST_CHECK(aborted);
    BOOST_CHECK_MESSAGE(!service->m_xNodesHeldDuringIsConnected.load(),
        "FIB-186 vector D: heartBeat held x_nodes while calling isConnected() (which takes "
        "x_sessions). That is the reverse of onConnect's order (x_sessions -> x_nodes via "
        "updateStaticNodes) and deadlocks under connection churn. heartBeat must snapshot "
        "m_staticNodes under x_nodes, release it, then do the isConnected()/asyncConnect() work.");

    service->disarm();
}

BOOST_AUTO_TEST_SUITE_END()
