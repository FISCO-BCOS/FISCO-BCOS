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
 * @file PeerManager.cpp
 * @brief Peer table implementation.
 * @date 2026/11/3
 */
#include "PeerManager.h"

#include <bcos-utilities/LogStream.h>
#include <algorithm>
#include <utility>

namespace bcos::devp2p::sync
{
PeerManager::PeerManager(PeerManagerConfig _config, Clock _clock)
  : m_config(_config), m_clock(std::move(_clock))
{
    if (!m_clock)
    {
        m_clock = []() { return std::chrono::steady_clock::now(); };
    }
    if (m_config.maxInbound == 0 || m_config.maxInbound > m_config.maxPeers)
    {
        m_config.maxInbound = m_config.maxPeers - m_config.maxPeers / 3;
    }
}

bool PeerManager::bannedLocked(NodeId const& _id) const
{
    auto it = m_bans.find(_id);
    if (it == m_bans.end())
    {
        return false;
    }
    if (m_clock() >= it->second)
    {
        m_bans.erase(it);
        return false;
    }
    return true;
}

std::optional<rlpx::DisconnectReason> PeerManager::tryAdmit(NodeId const& _id, Direction _direction)
{
    std::unique_lock lock(m_mutex);
    if (bannedLocked(_id))
    {
        return rlpx::DisconnectReason::UselessPeer;
    }
    if (m_connected.contains(_id))
    {
        return rlpx::DisconnectReason::AlreadyConnected;
    }
    if (m_connected.size() >= m_config.maxPeers)
    {
        return rlpx::DisconnectReason::TooManyPeers;
    }
    if (_direction == Direction::Inbound)
    {
        size_t inbound = 0;
        for (auto const& [id, peer] : m_connected)
        {
            inbound += peer.direction == Direction::Inbound ? 1 : 0;
        }
        if (inbound >= m_config.maxInbound)
        {
            return rlpx::DisconnectReason::TooManyPeers;
        }
    }
    m_connected.emplace(_id, Connected{_direction});
    return std::nullopt;
}

void PeerManager::release(NodeId const& _id)
{
    std::unique_lock lock(m_mutex);
    m_connected.erase(_id);
}

bool PeerManager::isConnected(NodeId const& _id) const
{
    std::unique_lock lock(m_mutex);
    return m_connected.contains(_id);
}

size_t PeerManager::connectedCount() const
{
    std::unique_lock lock(m_mutex);
    return m_connected.size();
}

size_t PeerManager::inboundCount() const
{
    std::unique_lock lock(m_mutex);
    size_t inbound = 0;
    for (auto const& [id, peer] : m_connected)
    {
        inbound += peer.direction == Direction::Inbound ? 1 : 0;
    }
    return inbound;
}

std::vector<PeerSnapshot> PeerManager::connectedPeers() const
{
    std::unique_lock lock(m_mutex);
    std::vector<PeerSnapshot> out;
    out.reserve(m_connected.size());
    for (auto const& [id, peer] : m_connected)
    {
        auto score = m_scores.find(id);
        out.push_back({id, peer.direction, score == m_scores.end() ? 0 : score->second});
    }
    return out;
}

void PeerManager::penalize(NodeId const& _id, Penalty _penalty)
{
    std::unique_lock lock(m_mutex);
    if (m_staticIds.contains(_id))
    {
        return;  // operator-configured peers are never scored out
    }
    int& score = m_scores[_id];
    score += static_cast<int>(_penalty);
    if (score >= m_config.banThreshold)
    {
        m_bans[_id] = m_clock() + m_config.banTtl;
        m_scores.erase(_id);
        BCOS_LOG(INFO) << LOG_BADGE("peer-manager") << "peer banned"
                       << LOG_KV("nodeId", bcos::toHex(_id).substr(0, 16))
                       << LOG_KV("ttlSeconds", m_config.banTtl.count());
    }
}

bool PeerManager::isBanned(NodeId const& _id) const
{
    std::unique_lock lock(m_mutex);
    return bannedLocked(_id);
}

int PeerManager::score(NodeId const& _id) const
{
    std::unique_lock lock(m_mutex);
    auto it = m_scores.find(_id);
    return it == m_scores.end() ? 0 : it->second;
}

void PeerManager::setStaticPeers(std::vector<rlpx::PeerConfig> _peers)
{
    std::unique_lock lock(m_mutex);
    m_static = std::move(_peers);
    m_staticIds.clear();
    for (auto const& peer : m_static)
    {
        m_staticIds.insert(peer.peerPublicKey);
    }
}

void PeerManager::addCandidate(rlpx::PeerConfig _peer)
{
    if (_peer.peerPublicKey.size() != 64)
    {
        return;
    }
    std::unique_lock lock(m_mutex);
    if (m_staticIds.contains(_peer.peerPublicKey) || m_discoveredIds.contains(_peer.peerPublicKey))
    {
        return;
    }
    while (m_discovered.size() >= m_config.maxCandidates && !m_discovered.empty())
    {
        m_discoveredIds.erase(m_discovered.front().peerPublicKey);
        m_discovered.pop_front();
    }
    m_discoveredIds.insert(_peer.peerPublicKey);
    m_discovered.push_back(std::move(_peer));
}

std::vector<rlpx::PeerConfig> PeerManager::dialCandidates() const
{
    std::unique_lock lock(m_mutex);
    std::vector<rlpx::PeerConfig> out;
    out.reserve(m_static.size() + m_discovered.size());
    auto const eligible = [&](rlpx::PeerConfig const& peer) {
        return !bannedLocked(peer.peerPublicKey) && !m_connected.contains(peer.peerPublicKey);
    };
    for (auto const& peer : m_static)
    {
        if (eligible(peer))
        {
            out.push_back(peer);
        }
    }
    for (auto const& peer : m_discovered)
    {
        if (eligible(peer))
        {
            out.push_back(peer);
        }
    }
    return out;
}

size_t PeerManager::candidateCount() const
{
    std::unique_lock lock(m_mutex);
    return m_static.size() + m_discovered.size();
}
}  // namespace bcos::devp2p::sync
