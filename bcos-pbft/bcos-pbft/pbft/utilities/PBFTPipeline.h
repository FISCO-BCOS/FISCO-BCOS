/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief 3-stage admission pipeline for PBFT messages before m_msgQueue push.
 *        Fixes FIB-145 (unbounded m_msgQueue during sync → OOM) and
 *        FIB-146 (re-queued future packets → CPU busy-spin + OOM).
 *
 * Pipeline stages:
 *   Stage 1 – Stale-height filter: drop messages whose index() < lastApplied.
 *             Commit and CheckPoint packets are NEVER dropped to preserve safety.
 *   Stage 2 – LRU per-peer dedup: drop messages already recently seen from
 *             the same peer (keyed by packetType + index + hash).
 *             Per-peer bounded LRU; evicts oldest entry on capacity overflow.
 *   Stage 3 – Per-type admission capacity:
 *             PrePrepare and Prepare: bounded queue size per-peer; on overflow set
 *             backpressure flag for that peer, suppress further PrePrepare/Prepare
 *             from that peer (Commit/CheckPoint from the same peer still admitted).
 *             Commit and CheckPoint: always admitted, never suppressed.
 *
 * @file PBFTPipeline.h
 * @author: kyonRay
 * @date 2026-05-07
 */
#pragma once
#include "bcos-pbft/pbft/interfaces/PBFTBaseMessageInterface.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/BoostLog.h>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <limits>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace bcos::consensus
{

// ─── Stage 2 LRU per-peer cache ─────────────────────────────────────────────
// FIB-146: real per-peer LRU dedup cache (replaces E.6a passthrough stub).
//
// Keys are strings of the form "<packetType>:<blockIndex>:<hashHex>".
// When capacity is reached, the least-recently-used entry is evicted to make
// room. This bounds per-peer memory while still detecting near-term duplicates.
//
// Thread-safety: NOT thread-safe. The caller (PBFTPipeline) holds m_lruMutex
// when calling seenAndInsert(), so no internal locking is needed.
class PeerLRUCache
{
public:
    explicit PeerLRUCache(size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    // Returns true if the key has been seen recently (should be dropped).
    // Inserts the key into the cache (evicting the LRU entry if full).
    bool seenAndInsert(std::string const& key)
    {
        const auto it = m_map.find(key);
        if (it != m_map.end())
        {
            // Cache hit: move to front (most recently used)
            m_list.splice(m_list.begin(), m_list, it->second);
            return true;
        }

        // Cache miss: evict LRU entry if at capacity
        if (m_list.size() >= m_capacity)
        {
            m_map.erase(m_list.back());
            m_list.pop_back();
        }

        // Insert new key at front
        m_list.push_front(key);
        m_map.emplace(key, m_list.begin());
        return false;
    }

    // Drop a key from the cache (no-op if absent). Used when a message is
    // consumed from m_msgQueue so subsequent legitimate retransmissions /
    // next-round messages with a colliding key are not blocked.
    bool evictKey(std::string const& key)
    {
        const auto it = m_map.find(key);
        if (it == m_map.end())
        {
            return false;
        }
        m_list.erase(it->second);
        m_map.erase(it);
        return true;
    }

    bool empty() const { return m_list.empty(); }

private:
    size_t m_capacity;
    std::list<std::string> m_list;  // front = most recently used
    std::unordered_map<std::string, std::list<std::string>::iterator> m_map;
};

// ─── Admission pipeline ───────────────────────────────────────────────────────
class PBFTPipeline
{
public:
    using MessagePtr = std::shared_ptr<PBFTBaseMessageInterface>;
    using PeerID = std::string;  // hex string of peer node-id

    // Packet types that are safety-critical and must never be dropped
    static bool isSafetyCritical(PacketType type)
    {
        return type == PacketType::CommitPacket || type == PacketType::CheckPoint;
    }

    struct Config
    {
        // Master switch. When false, admit() always returns true (full bypass).
        bool enabled{true};

        // Stage 3: max pending PrePrepare+Prepare messages per peer before backpressure
        size_t perPeerCapacity{64};

        // Stage 2: per-peer LRU dedup cache capacity (FIB-146).
        size_t lruCapacity{256};

        // Cap on the number of distinct peers tracked at once. When exceeded,
        // the least-recently-used peer's entire LRU cache is evicted to make
        // room (FIB-146 follow-up).
        size_t maxPeers{1024};
    };

    explicit PBFTPipeline(Config cfg) : m_cfg(cfg) {}
    PBFTPipeline() : m_cfg{} {}

    // Reconfigure in place. Only safe to call before any concurrent admit()
    // begins (i.e., during engine construction or while the worker is paused).
    // PBFTPipeline holds mutex members so is non-movable; this method lets the
    // engine apply node.ini config without recreating the instance.
    void configure(Config cfg) noexcept { m_cfg = cfg; }

    // Return true if the message should be admitted to m_msgQueue.
    // - lastApplied: Stage 1 stale-height filter drops messages strictly below this.
    // - maxFutureIndex: Stage 1b future-height filter drops messages strictly above
    //   this (FIB-146). Caller should pass committedIndex + 2 * waterMarkLimit().
    //   Defaults to INT64_MAX so legacy two-arg callers keep working.
    bool admit(MessagePtr const& msg, bcos::protocol::BlockNumber lastApplied,
        bcos::protocol::BlockNumber maxFutureIndex = std::numeric_limits<int64_t>::max())
    {
        // Master switch — disabled pipeline admits everything (bypass).
        if (!m_cfg.enabled)
        {
            return true;
        }

        auto peerKey = peerIDKey(msg);
        auto type = msg->packetType();

        // ── Stage 1: stale-height filter ────────────────────────────────────
        // Drop messages whose height is strictly below lastApplied,
        // UNLESS they are safety-critical (Commit/CheckPoint).
        if (!isSafetyCritical(type) && msg->index() < lastApplied)
        {
            PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage1: drop stale msg")
                            << LOG_KV("type", type) << LOG_KV("index", msg->index())
                            << LOG_KV("lastApplied", lastApplied);
            return false;
        }

        // ── Stage 1b: future-height filter (FIB-146) ────────────────────────
        // Drop messages whose index is more than 2 × pipeline-width past the
        // last applied block. Safety-critical packets (Commit/CheckPoint) bypass
        // this filter for the same reason they bypass Stage 1 — never drop
        // messages required for consensus liveness. The cap prevents a Byzantine
        // peer from filling m_msgQueue with far-future packets that the worker
        // would otherwise repeatedly pop/re-push (CPU busy-spin).
        if (!isSafetyCritical(type) && msg->index() > maxFutureIndex)
        {
            PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage1b: drop far-future msg")
                            << LOG_KV("type", type) << LOG_KV("index", msg->index())
                            << LOG_KV("maxFutureIndex", maxFutureIndex);
            return false;
        }

        // ── Stage 2: LRU per-peer dedup (FIB-146) ───────────────────────────
        // Drop messages whose (type:index:hash) tuple was recently seen from
        // this peer AND is currently in-flight in m_msgQueue. Per-peer LRU
        // eviction bounds total tracked peers at m_cfg.maxPeers.
        //
        // Important: the entry is removed in consumed() when the message is
        // popped off m_msgQueue, so legitimate retransmissions / next-round
        // messages whose (type,index,hash) collides with an already-handled
        // packet are not permanently blocked. See FIB-146 followup notes.
        {
            std::string dedupKey = makeDedupKey(msg);
            if (dedupSeenAndInsert(peerKey, dedupKey))
            {
                PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage2: drop dedup msg")
                                << LOG_KV("type", type) << LOG_KV("index", msg->index());
                return false;
            }
        }

        // ── Stage 3: per-type admission capacity ─────────────────────────────
        // Commit/CheckPoint: always admitted.
        if (isSafetyCritical(type))
        {
            return true;
        }

        // PrePrepare/Prepare and others: check per-peer backpressure and capacity.
        {
            std::scoped_lock lock(m_mutex);

            // If peer is under backpressure, suppress PrePrepare/Prepare
            if (m_backpressuredPeers.contains(peerKey))
            {
                PBFT_LOG(TRACE) << LOG_DESC("[PBFTPipeline] Stage3: drop backpressured peer msg")
                                << LOG_KV("type", type) << LOG_KV("index", msg->index());
                return false;
            }

            auto& count = m_peerPendingCount[peerKey];
            if (count >= m_cfg.perPeerCapacity)
            {
                // Set backpressure for this peer
                m_backpressuredPeers.insert(peerKey);
                PBFT_LOG(INFO) << LOG_DESC(
                                      "[PBFTPipeline] Stage3: peer capacity exceeded, "
                                      "setting backpressure")
                               << LOG_KV("peer", peerKey)
                               << LOG_KV("capacity", m_cfg.perPeerCapacity) << LOG_KV("type", type);
                return false;
            }
            ++count;
        }
        return true;
    }

    // Call when a message is consumed from m_msgQueue.
    //   * Decrements the per-peer pending counter (Stage 3).
    //   * Evicts the Stage 2 dedup entry so legitimate retransmissions /
    //     next-round messages whose (type,index,hash) collides with an
    //     already-handled packet are not permanently blocked.
    void consumed(MessagePtr const& msg)
    {
        auto peerKey = peerIDKey(msg);
        auto type = msg->packetType();

        // Stage 2 dedup eviction always runs (even for safety-critical packets)
        // so re-arrivals after handling are admitted.
        evictDedupEntry(peerKey, makeDedupKey(msg));

        if (isSafetyCritical(type))
        {
            return;  // safety-critical messages weren't counted
        }
        std::scoped_lock lock(m_mutex);
        auto it = m_peerPendingCount.find(peerKey);
        if (it != m_peerPendingCount.end() && it->second > 0)
        {
            --it->second;
            if (it->second < m_cfg.perPeerCapacity / 2)
            {
                // Clear backpressure when queue drains to half capacity
                m_backpressuredPeers.erase(peerKey);
            }
        }
    }

    // Reset all per-peer pipeline state. Called by the engine on state
    // discontinuities (view-change completion, sealer-set rotation) where
    // continuing to dedup against the previous epoch's traffic offers no
    // value. Both Stage 2 (LRU dedup caches + peer-LRU tracker) and Stage 3
    // (per-peer pending counters + backpressure set) are cleared.
    void reset()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_backpressuredPeers.clear();
            m_peerPendingCount.clear();
        }
        {
            std::scoped_lock lock(m_lruMutex);
            m_lruCaches.clear();
            m_peerLruOrder.clear();
            m_peerLruIndex.clear();
        }
    }

    bool isPeerBackpressured(PeerID const& peer) const
    {
        std::scoped_lock lock(m_mutex);
        return m_backpressuredPeers.contains(peer);
    }

    size_t peerPendingCount(PeerID const& peer) const
    {
        std::scoped_lock lock(m_mutex);
        auto it = m_peerPendingCount.find(peer);
        return it != m_peerPendingCount.end() ? it->second : 0U;
    }

private:
    static PeerID peerIDKey(MessagePtr const& msg)
    {
        auto from = msg->from();
        return from ? from->hex() : "unknown";
    }

    // Build the Stage 2 dedup key. Must match in admit() and consumed().
    static std::string makeDedupKey(MessagePtr const& msg)
    {
        return std::to_string(msg->packetType()) + ":" + std::to_string(msg->index()) + ":" +
               msg->hash().hex();
    }

    // Drop a Stage 2 dedup entry for the given peer. If the peer's per-peer
    // cache becomes empty, also drop the peer from the maxPeers LRU tracker
    // so the slot can be reused (otherwise idle peers would slowly fill the
    // tracker even though their cache holds nothing).
    void evictDedupEntry(PeerID const& peer, std::string const& dedupKey)
    {
        std::scoped_lock lock(m_lruMutex);
        auto it = m_lruCaches.find(peer);
        if (it == m_lruCaches.end())
        {
            return;
        }
        it->second.evictKey(dedupKey);
        if (it->second.empty())
        {
            auto idx = m_peerLruIndex.find(peer);
            if (idx != m_peerLruIndex.end())
            {
                m_peerLruOrder.erase(idx->second);
                m_peerLruIndex.erase(idx);
            }
            m_lruCaches.erase(it);
        }
    }

    // FIB-146 follow-up: dedup check + peer-LRU eviction in a single critical
    // section. Returns true if the key was already present (drop the message);
    // false if newly inserted (admit). Replaces the previous getOrCreateCache
    // pattern which leaked a reference out of the lock, exposing the per-peer
    // cache to data races and (post-eviction) use-after-erase.
    bool dedupSeenAndInsert(PeerID const& peer, std::string const& dedupKey)
    {
        std::scoped_lock lock(m_lruMutex);
        auto it = m_lruCaches.find(peer);
        if (it != m_lruCaches.end())
        {
            // Bump peer to MRU in the peer-LRU tracker
            m_peerLruOrder.splice(m_peerLruOrder.begin(), m_peerLruOrder, m_peerLruIndex.at(peer));
            return it->second.seenAndInsert(dedupKey);
        }

        // New peer: enforce maxPeers cap by evicting LRU peer if needed
        if (m_lruCaches.size() >= m_cfg.maxPeers)
        {
            auto const& victim = m_peerLruOrder.back();
            m_lruCaches.erase(victim);
            m_peerLruIndex.erase(victim);
            m_peerLruOrder.pop_back();
        }

        // Insert new peer at MRU position
        m_peerLruOrder.push_front(peer);
        m_peerLruIndex.emplace(peer, m_peerLruOrder.begin());
        auto [inserted, _] = m_lruCaches.emplace(peer, PeerLRUCache{m_cfg.lruCapacity});
        return inserted->second.seenAndInsert(dedupKey);
    }

    Config m_cfg;

    mutable std::mutex m_mutex;
    std::unordered_map<PeerID, size_t> m_peerPendingCount;
    std::unordered_set<PeerID> m_backpressuredPeers;

    // FIB-146 follow-up: m_lruMutex now guards three coupled structures —
    // m_lruCaches (cache storage), m_peerLruOrder (front = MRU peer), and
    // m_peerLruIndex (peer → iterator into m_peerLruOrder). All three must
    // be mutated together when admitting / evicting; see dedupSeenAndInsert.
    std::mutex m_lruMutex;
    std::unordered_map<PeerID, PeerLRUCache> m_lruCaches;
    std::list<PeerID> m_peerLruOrder;
    std::unordered_map<PeerID, std::list<PeerID>::iterator> m_peerLruIndex;
};

}  // namespace bcos::consensus
