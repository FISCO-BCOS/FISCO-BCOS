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
 * @file PeerManager.h
 * @brief The one table of devp2p peers shared by the inbound listener and the outbound
 *        dial loop: slot accounting (max_peers with an inbound share), dedupe by node
 *        id, per-peer misbehaviour score with a TTL ban list, and the dial-candidate
 *        set (static bootnodes plus whatever discovery hands in via addCandidate).
 * @date 2026/11/3
 */
#pragma once

#include "../rlpx/Client.h"
#include "../rlpx/Messages.h"
#include <bcos-utilities/Common.h>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace bcos::devp2p::sync
{
// 64-byte uncompressed secp256k1 public key (PeerConfig::peerPublicKey / Hello id).
using NodeId = bcos::bytes;

struct PeerManagerConfig
{
    size_t maxPeers{50};
    // Slots inbound peers may fill; the rest stays reserved for our own dials
    // (geth: MaxPeers - MaxPeers/DialRatio). 0 = derive as maxPeers - maxPeers/3.
    size_t maxInbound{0};
    // A peer whose score reaches this is banned for banTtl.
    int banThreshold{100};
    std::chrono::seconds banTtl{15 * 60};
    // Discovered candidates kept (oldest dropped first).
    size_t maxCandidates{1024};
};

enum class Direction
{
    Inbound,
    Outbound,
};

// Score added per misbehaviour; static (operator-configured) peers are exempt.
enum class Penalty : int
{
    Timeout = 20,          // request or ping timed out
    UselessResponse = 10,  // empty or off-request answer
    HandshakeFailed = 10,  // dial reached TCP but the handshake failed
    Malformed = 50,        // undecodable frame / protocol breach
};

struct PeerSnapshot
{
    NodeId id;
    Direction direction{Direction::Outbound};
    int score{0};
};

class PeerManager
{
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    explicit PeerManager(PeerManagerConfig _config = {}, Clock _clock = nullptr);

    // --- connected peers ---
    // Claim a slot for a peer that completed the handshake. nullopt = admitted (hold the
    // slot until release()); otherwise the Disconnect reason to send: AlreadyConnected,
    // TooManyPeers, or UselessPeer for a banned node. Directions are deduped separately:
    // an inbound session only serves, so a peer dialing us must not stop us syncing from it.
    std::optional<rlpx::DisconnectReason> tryAdmit(NodeId const& _id, Direction _direction);
    void release(NodeId const& _id, Direction _direction);
    bool isConnected(NodeId const& _id) const;
    size_t connectedCount() const;
    size_t inboundCount() const;
    std::vector<PeerSnapshot> connectedPeers() const;

    // --- scoring / bans ---
    void penalize(NodeId const& _id, Penalty _penalty);
    bool isBanned(NodeId const& _id) const;
    int score(NodeId const& _id) const;

    // --- dial candidates ---
    // The operator's bootnodes (re-read by the dial loop each round); never penalised.
    void setStaticPeers(std::vector<rlpx::PeerConfig> _peers);
    // Discovery hand-off (thread-safe, any thread): a dialable peer learned at runtime.
    // Deduped by node id: a static id is ignored, a known discovered id has its endpoint
    // refreshed in place; the oldest discovered entry is dropped past maxCandidates.
    // Entries without a 64-byte key are ignored.
    void addCandidate(rlpx::PeerConfig _peer);
    // Static peers first, then up to _maxDiscovered discovered ones (rotating between calls),
    // minus banned nodes and nodes we already dialed. The dial loop walks this list.
    std::vector<rlpx::PeerConfig> dialCandidates(
        size_t _maxDiscovered = std::numeric_limits<size_t>::max()) const;
    // Whether the operator configured this node. Only these peers are trusted to pick the
    // chain; a discovered peer is an unauthenticated stranger.
    bool isStatic(NodeId const& _id) const;
    size_t candidateCount() const;

    PeerManagerConfig const& config() const { return m_config; }

private:
    bool bannedLocked(NodeId const& _id) const;  // expires stale bans; m_mutex held

    PeerManagerConfig m_config;
    Clock m_clock;
    mutable std::mutex m_mutex;
    std::set<NodeId> m_inbound;
    std::set<NodeId> m_outbound;
    std::map<NodeId, int> m_scores;
    mutable std::map<NodeId, std::chrono::steady_clock::time_point> m_bans;  // expiry
    std::vector<rlpx::PeerConfig> m_static;
    std::set<NodeId> m_staticIds;
    std::deque<rlpx::PeerConfig> m_discovered;
    std::set<NodeId> m_discoveredIds;
    mutable size_t m_discoveredCursor{0};
};
}  // namespace bcos::devp2p::sync
