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
 * @file PeerManagerTest.cpp
 * @brief Peer table: slot caps with the inbound share, dedupe, release, scoring into a
 *        TTL ban (fake clock), static-peer exemption, and the dial-candidate list fed
 *        by setStaticPeers/addCandidate.
 * @date 2026/11/3
 */
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/sync/PeerManager.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(PeerManagerTest)

namespace
{
sync::NodeId nodeId(uint8_t _tag)
{
    sync::NodeId id(64, 0);
    id[63] = _tag;
    return id;
}

rlpx::PeerConfig peer(uint8_t _tag)
{
    rlpx::PeerConfig config;
    config.host = "10.0.0." + std::to_string(_tag);
    config.port = 30303;
    config.peerPublicKey = nodeId(_tag);
    return config;
}
}  // namespace

BOOST_AUTO_TEST_CASE(slotsDedupeAndInboundShare)
{
    sync::PeerManagerConfig config;
    config.maxPeers = 3;  // inbound share derives to 3 - 1 = 2
    sync::PeerManager manager(config);
    BOOST_CHECK_EQUAL(manager.config().maxInbound, 2u);

    BOOST_CHECK(!manager.tryAdmit(nodeId(1), sync::Direction::Inbound).has_value());
    BOOST_CHECK(manager.tryAdmit(nodeId(1), sync::Direction::Inbound) ==
                rlpx::DisconnectReason::AlreadyConnected);
    BOOST_CHECK(!manager.tryAdmit(nodeId(2), sync::Direction::Inbound).has_value());
    // Third inbound exceeds the inbound share while a slot is still free for a dial.
    BOOST_CHECK(manager.tryAdmit(nodeId(3), sync::Direction::Inbound) ==
                rlpx::DisconnectReason::TooManyPeers);
    BOOST_CHECK(!manager.tryAdmit(nodeId(3), sync::Direction::Outbound).has_value());
    BOOST_CHECK(manager.tryAdmit(nodeId(4), sync::Direction::Outbound) ==
                rlpx::DisconnectReason::TooManyPeers);
    BOOST_CHECK_EQUAL(manager.connectedCount(), 3u);
    BOOST_CHECK_EQUAL(manager.inboundCount(), 2u);
    BOOST_CHECK(manager.isConnected(nodeId(2)));

    manager.release(nodeId(2), sync::Direction::Inbound);
    BOOST_CHECK(!manager.isConnected(nodeId(2)));
    BOOST_CHECK(!manager.tryAdmit(nodeId(4), sync::Direction::Inbound).has_value());
    BOOST_CHECK_EQUAL(manager.connectedPeers().size(), 3u);
}

// An inbound session only serves: a bootnode that dialed us must stay dialable, or we
// would stop syncing from our only peer. Regression for one table keyed by id alone.
BOOST_AUTO_TEST_CASE(inboundSessionDoesNotBlockSyncDial)
{
    sync::PeerManager manager;
    manager.setStaticPeers({peer(1)});
    BOOST_CHECK(!manager.tryAdmit(nodeId(1), sync::Direction::Inbound).has_value());
    BOOST_CHECK_EQUAL(manager.dialCandidates().size(), 1u);
    BOOST_CHECK(!manager.tryAdmit(nodeId(1), sync::Direction::Outbound).has_value());
    BOOST_CHECK(manager.dialCandidates().empty());
    BOOST_CHECK_EQUAL(manager.connectedCount(), 2u);
    BOOST_CHECK_EQUAL(manager.inboundCount(), 1u);
    manager.release(nodeId(1), sync::Direction::Outbound);
    BOOST_CHECK(manager.isConnected(nodeId(1)));
    manager.release(nodeId(1), sync::Direction::Inbound);
    BOOST_CHECK(!manager.isConnected(nodeId(1)));
}

// A discovered peer is never static, the dial list honours the discovered cap, and banned
// peers do not use it up.
BOOST_AUTO_TEST_CASE(discoveredCapAndTrust)
{
    sync::PeerManager manager;
    manager.setStaticPeers({peer(1)});
    manager.addCandidate(peer(10));
    manager.addCandidate(peer(11));
    manager.addCandidate(peer(12));
    BOOST_CHECK(manager.isStatic(nodeId(1)));
    BOOST_CHECK(!manager.isStatic(nodeId(10)));
    auto candidates = manager.dialCandidates(0);
    BOOST_REQUIRE_EQUAL(candidates.size(), 1u);
    BOOST_CHECK_EQUAL(candidates[0].host, "10.0.0.1");
    manager.penalize(nodeId(10), sync::Penalty::Malformed);
    manager.penalize(nodeId(10), sync::Penalty::Malformed);
    candidates = manager.dialCandidates(1);
    BOOST_REQUIRE_EQUAL(candidates.size(), 2u);
    BOOST_CHECK_EQUAL(candidates[1].host, "10.0.0.11");
}

// With very few slots at least one stays free for our own dial.
BOOST_AUTO_TEST_CASE(smallTableKeepsADialSlot)
{
    sync::PeerManagerConfig config;
    config.maxPeers = 2;
    sync::PeerManager manager(config);
    BOOST_CHECK_EQUAL(manager.config().maxInbound, 1u);
    BOOST_CHECK(!manager.tryAdmit(nodeId(1), sync::Direction::Inbound).has_value());
    BOOST_CHECK(manager.tryAdmit(nodeId(2), sync::Direction::Inbound) ==
                rlpx::DisconnectReason::TooManyPeers);
    BOOST_CHECK(!manager.tryAdmit(nodeId(3), sync::Direction::Outbound).has_value());
}

BOOST_AUTO_TEST_CASE(scoringBansWithTtl)
{
    using namespace std::chrono;
    auto now = steady_clock::time_point{} + hours(1);
    sync::PeerManagerConfig config;
    config.banThreshold = 60;
    config.banTtl = seconds(100);
    sync::PeerManager manager(config, [&]() { return now; });

    manager.penalize(nodeId(7), sync::Penalty::Timeout);
    manager.penalize(nodeId(7), sync::Penalty::UselessResponse);
    BOOST_CHECK_EQUAL(manager.score(nodeId(7)), 30);
    BOOST_CHECK(!manager.isBanned(nodeId(7)));
    manager.penalize(nodeId(7), sync::Penalty::Malformed);  // 80 >= 60
    BOOST_CHECK(manager.isBanned(nodeId(7)));
    BOOST_CHECK_EQUAL(manager.score(nodeId(7)), 0);  // score reset on ban
    BOOST_CHECK(manager.tryAdmit(nodeId(7), sync::Direction::Inbound) ==
                rlpx::DisconnectReason::UselessPeer);
    // Ban expires with the clock.
    now += seconds(99);
    BOOST_CHECK(manager.isBanned(nodeId(7)));
    now += seconds(2);
    BOOST_CHECK(!manager.isBanned(nodeId(7)));
    BOOST_CHECK(!manager.tryAdmit(nodeId(7), sync::Direction::Inbound).has_value());

    // Static (operator) peers are never scored.
    manager.setStaticPeers({peer(9)});
    for (int i = 0; i < 10; ++i)
    {
        manager.penalize(nodeId(9), sync::Penalty::Malformed);
    }
    BOOST_CHECK(!manager.isBanned(nodeId(9)));
    BOOST_CHECK_EQUAL(manager.score(nodeId(9)), 0);
}

BOOST_AUTO_TEST_CASE(dialCandidates)
{
    sync::PeerManagerConfig config;
    config.maxCandidates = 3;
    config.banThreshold = 50;
    sync::PeerManager manager(config);
    manager.setStaticPeers({peer(1), peer(2)});
    manager.addCandidate(peer(2));  // duplicate of a static peer: ignored
    manager.addCandidate(peer(10));
    manager.addCandidate(peer(10));  // duplicate: ignored
    rlpx::PeerConfig keyless;
    keyless.host = "1.2.3.4";
    manager.addCandidate(keyless);  // no node id: ignored
    BOOST_CHECK_EQUAL(manager.candidateCount(), 3u);

    auto candidates = manager.dialCandidates();
    BOOST_REQUIRE_EQUAL(candidates.size(), 3u);
    BOOST_CHECK_EQUAL(candidates[0].host, "10.0.0.1");
    BOOST_CHECK_EQUAL(candidates[1].host, "10.0.0.2");
    BOOST_CHECK_EQUAL(candidates[2].host, "10.0.0.10");

    // Connected and banned nodes drop out of the dial list.
    BOOST_CHECK(!manager.tryAdmit(nodeId(1), sync::Direction::Outbound).has_value());
    manager.penalize(nodeId(10), sync::Penalty::Malformed);
    candidates = manager.dialCandidates();
    BOOST_REQUIRE_EQUAL(candidates.size(), 1u);
    BOOST_CHECK_EQUAL(candidates[0].host, "10.0.0.2");
    manager.release(nodeId(1), sync::Direction::Outbound);
    BOOST_CHECK_EQUAL(manager.dialCandidates().size(), 2u);

    // The discovered set is bounded: oldest dropped first.
    manager.addCandidate(peer(11));
    manager.addCandidate(peer(12));
    manager.addCandidate(peer(13));
    manager.addCandidate(peer(14));
    candidates = manager.dialCandidates();
    // static 1, 2 + discovered 12, 13, 14 (10 banned and evicted, 11 evicted)
    BOOST_REQUIRE_EQUAL(candidates.size(), 5u);
    BOOST_CHECK_EQUAL(candidates[2].host, "10.0.0.12");
    BOOST_CHECK_EQUAL(candidates[4].host, "10.0.0.14");
    // Replacing the static list re-admits a formerly static id as discoverable.
    manager.setStaticPeers({peer(2)});
    manager.addCandidate(peer(1));
    BOOST_CHECK_EQUAL(manager.dialCandidates().back().host, "10.0.0.1");
}

BOOST_AUTO_TEST_SUITE_END()
