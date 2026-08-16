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
 * The fix coalesces the broadcasts: the first membership change of a burst broadcasts once (leading
 * edge) and the rest only advance the seq, which the 3s m_routerTimer flushes. This test drives a
 * burst of onNewSession / onEraseSession calls and asserts the broadcast count is exactly one --
 * not one-per-change (RED on the pre-fix per-change broadcast, the gossip storm) and not zero (RED
 * on deleting the event-driven broadcast, which would serialize route convergence behind the 3s
 * timer). The isReachable() precondition guarantees the handler reached the router-table update, so
 * the assertion is not vacuously satisfied by an early return.
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <atomic>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB186_RouterSeqDebounceTest, TestPromptFixture)

namespace
{
// onNewSession/onEraseSession read p2pID()/printP2pID()/p2pInfo() off the session. Populate p2pInfo
// directly (mutableP2pInfo avoids setP2PInfo, which dereferences the session's socket) so the
// router entry's dstNode (p2pID) and dstNodeInfo (p2pInfo.rawP2pID) agree -- avoiding the
// empty-p2pInfo state a real session never produces.
class FakeSessionVB : public P2PSession
{
public:
    explicit FakeSessionVB(std::string _id) : m_id(std::move(_id))
    {
        auto info = mutableP2pInfo();
        info->rawP2pID = m_id;
        info->p2pID = m_id;
    }
    P2pID p2pID() override { return m_id; }
    std::string printP2pID() override { return m_id; }
    std::string m_id;
};

// Counts broadcastRouterSeq() (overriding away the real broadcast) and exposes the protected
// membership handlers so the test can drive them directly.
class CountingServiceV2 : public ServiceV2
{
public:
    // ServiceV2 borrows an external io_context now; the test owns it and passes it in.
    CountingServiceV2(
        P2PInfo const& _info, RouterTableFactory::Ptr _factory, boost::asio::io_context& _ioContext)
      : ServiceV2(_info, std::move(_factory), _ioContext)
    {}
    void broadcastRouterSeq() override { ++m_broadcastCount; }
    void callOnNewSession(P2PSession::Ptr _session) { onNewSession(std::move(_session)); }
    void callOnEraseSession(P2PSession::Ptr _session) { onEraseSession(std::move(_session)); }
    std::atomic<int> m_broadcastCount{0};
};
}  // namespace

BOOST_AUTO_TEST_CASE(MembershipChurnCoalescesRouterSeqToOneLeadingEdgeBroadcast)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto factory = std::make_shared<RouterTableFactoryImpl>();
    boost::asio::io_context ioContext;
    auto service = std::make_shared<CountingServiceV2>(selfInfo, factory, ioContext);

    // A connect/disconnect flood drives many membership changes in quick succession. On the pre-fix
    // code each one called broadcastRouterSeq() -> one broadcast per change (the gossip storm). The
    // debounced code broadcasts once on the first change (leading edge) and coalesces the rest
    // until the m_routerTimer flush, which is not running in this unit test, so the dirty flag
    // stays set.
    constexpr int kChurn = 8;
    std::vector<std::shared_ptr<FakeSessionVB>> peers;
    for (int i = 0; i < kChurn; ++i)
    {
        auto peer = std::make_shared<FakeSessionVB>("peer-" + std::to_string(i));
        peers.push_back(peer);
        service->callOnNewSession(peer);
    }

    BOOST_REQUIRE_MESSAGE(service->isReachable("peer-0"),
        "precondition: onNewSession must have recorded the peers in the router table, else the "
        "assertion below would be vacuously satisfied by an early return");
    BOOST_CHECK_MESSAGE(service->m_broadcastCount.load() == 1,
        "FIB-186 vector B: a burst of membership changes must coalesce to exactly one leading-edge "
        "router-seq broadcast -- not one per change (the gossip storm that starved the PBFT "
        "delivery pool) and not zero (which would serialize route convergence behind the 3s "
        "timer).");

    // Erasing the peers within the same (un-flushed) window stays coalesced -- still one broadcast.
    for (auto const& peer : peers)
    {
        service->callOnEraseSession(peer);
    }
    BOOST_CHECK_MESSAGE(service->m_broadcastCount.load() == 1,
        "FIB-186 vector B: erase churn within the same window must also coalesce; the coalesced "
        "seq is flushed once by the m_routerTimer, not once per erase.");

    service->stop();
}

BOOST_AUTO_TEST_SUITE_END()
