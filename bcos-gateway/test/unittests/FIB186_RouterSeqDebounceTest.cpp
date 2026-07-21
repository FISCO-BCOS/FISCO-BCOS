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
 * @brief Regression test for FIB-186 vector B: connection churn amplifying into a router-table
 *        gossip storm that starves PBFT delivery.
 * @file FIB186_RouterSeqDebounceTest.cpp
 * @date 2026-07-21
 *
 * On the pre-fix code every membership change (onNewSession / onEraseSession) and every learned
 * route (joinRouterTable) called broadcastRouterSeq() synchronously. Under a connect/disconnect
 * flood that cascaded a full-mesh seq -> request -> whole-table gossip, and all of it ran on
 * Host::m_asyncGroup -- the same reactor that delivers PBFT messages -- so consensus was starved.
 *
 * The fix removes the synchronous broadcasts from the membership handlers and relies on the
 * existing 3s m_routerTimer to broadcast the current seq. This test drives onNewSession /
 * onEraseSession and asserts that neither broadcasts synchronously. It is RED on the pre-fix code
 * (broadcast count > 0) and GREEN after the fix (count == 0). The isReachable() precondition
 * guarantees the handler actually reached the point where the pre-fix code would have broadcast, so
 * the assertion is not vacuously satisfied by an early return.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB186_RouterSeqDebounceTest, TestPromptFixture)

namespace
{
// onNewSession/onEraseSession only read p2pID()/printP2pID()/p2pInfo() off the session.
class FakeSessionVB : public P2PSession
{
public:
    explicit FakeSessionVB(std::string _id) : m_id(std::move(_id)) {}
    P2pID p2pID() override { return m_id; }
    std::string printP2pID() override { return m_id; }
    std::string m_id;
};

// Counts broadcastRouterSeq() (overriding away the real broadcast) and exposes the protected
// membership handlers so the test can drive them directly.
class CountingServiceV2 : public ServiceV2
{
public:
    CountingServiceV2(P2PInfo const& _info, RouterTableFactory::Ptr _factory)
      : ServiceV2(_info, std::move(_factory))
    {}
    void broadcastRouterSeq() override { ++m_broadcastCount; }
    void callOnNewSession(P2PSession::Ptr _session) { onNewSession(std::move(_session)); }
    void callOnEraseSession(P2PSession::Ptr _session) { onEraseSession(std::move(_session)); }
    std::atomic<int> m_broadcastCount{0};
};
}  // namespace

BOOST_AUTO_TEST_CASE(MembershipChangeDoesNotBroadcastRouterSeqSynchronously)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto factory = std::make_shared<RouterTableFactoryImpl>();
    auto service = std::make_shared<CountingServiceV2>(selfInfo, factory);

    auto session = std::make_shared<FakeSessionVB>("peerRawP2pID");

    // A new peer updates the router table -- on the pre-fix code this is the path that broadcast.
    service->callOnNewSession(session);
    BOOST_REQUIRE_MESSAGE(service->isReachable("peerRawP2pID"),
        "precondition: onNewSession must have recorded the peer in the router table, else the "
        "no-broadcast assertion below would be vacuously satisfied by an early return");
    BOOST_CHECK_MESSAGE(service->m_broadcastCount.load() == 0,
        "FIB-186 vector B: onNewSession broadcast the router seq synchronously. It must only "
        "advance "
        "the seq and let the 3s m_routerTimer broadcast, otherwise connection churn cascades a "
        "full-mesh gossip storm on the PBFT delivery pool.");

    // Erasing the peer must also not broadcast synchronously.
    service->callOnEraseSession(session);
    BOOST_CHECK_MESSAGE(service->m_broadcastCount.load() == 0,
        "FIB-186 vector B: onEraseSession broadcast the router seq synchronously; it must defer to "
        "the 3s m_routerTimer.");

    service->stop();
}

BOOST_AUTO_TEST_SUITE_END()
