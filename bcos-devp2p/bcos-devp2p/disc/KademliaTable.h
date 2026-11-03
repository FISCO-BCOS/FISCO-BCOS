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
 * @file KademliaTable.h
 * @brief discv4 routing table: 256 k-buckets (k = 16) keyed by
 *        keccak256(nodeId) XOR distance, per-bucket replacement cache and the
 *        endpoint-proof (ping/pong) bookkeeping each entry carries.
 * @date 2026/11/3
 */
#pragma once

#include "Enr.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace bcos::devp2p::disc
{
// discv4.md "Endpoint Proof": a pong within the last 12 hours verifies the sender.
constexpr std::chrono::hours c_bondExpiration{12};

struct NodeEntry
{
    using Clock = std::chrono::steady_clock;

    bcos::bytes publicKey;  // 64 bytes
    bcos::h256 id;          // keccak256(publicKey)
    bcos::bytes ip;         // 4 or 16 bytes
    uint16_t udp{0};
    uint16_t tcp{0};
    uint64_t enrSeq{0};         // last sequence number the node announced (ping/pong)
    std::optional<Enr> record;  // its verified record, once fetched (ENRRequest) or given (DNS)

    // Endpoint-proof bookkeeping. lastPongReceived: the node answered OUR ping — it is
    // verified and may send us FindNode/ENRRequest. lastPingReceived: the node pinged US
    // (and got our pong) — it most likely holds a proof for us, so we may send it FindNode
    // without pinging first.
    Clock::time_point lastPongReceived{};
    Clock::time_point lastPingReceived{};
    Clock::time_point addedAt{};
    uint32_t findNodeFailures{0};

    bool bonded(Clock::time_point _now) const
    {
        return lastPongReceived != Clock::time_point{} &&
               _now - lastPongReceived < c_bondExpiration;
    }
    bool hasProofOfUs(Clock::time_point _now) const
    {
        return lastPingReceived != Clock::time_point{} &&
               _now - lastPingReceived < c_bondExpiration;
    }
};

class KademliaTable
{
public:
    static constexpr size_t c_bucketCount = 256;
    static constexpr size_t c_bucketSize = 16;            // k
    static constexpr size_t c_replacementCacheSize = 10;  // geth maxReplacements

    explicit KademliaTable(bcos::h256 _selfId) : m_selfId(_selfId) {}

    static bcos::h256 nodeIdOf(bytesConstRef _publicKey);
    static bcos::h256 xorDistance(bcos::h256 const& _a, bcos::h256 const& _b);
    // Position of the highest differing bit, 1..256; 0 when equal (bucket = logDistance - 1).
    static int logDistance(bcos::h256 const& _a, bcos::h256 const& _b);

    enum class AddResult
    {
        Added,        // placed in its bucket
        Updated,      // already present; endpoint refreshed
        Replacement,  // bucket full; kept in the replacement cache
        Self,         // our own id, ignored
    };
    // Insert or refresh. On Updated the endpoint and enrSeq/record are taken from `_entry`
    // when set, timestamps keep the later of the two.
    AddResult add(NodeEntry _entry);
    // Bucket entries and replacements alike.
    NodeEntry* find(bcos::h256 const& _id);
    NodeEntry const* find(bcos::h256 const& _id) const;
    // Drop a node; a bucket slot freed this way is refilled from the replacement cache.
    bool remove(bcos::h256 const& _id);
    // Mark as most recently seen (moves to the bucket tail).
    void touch(bcos::h256 const& _id);

    // The `_count` bucket entries closest to `_target` by XOR distance.
    std::vector<NodeEntry> closest(bcos::h256 const& _target, size_t _count) const;
    // Least-recently-seen entry of a random non-empty bucket (the revalidation target).
    std::optional<bcos::h256> revalidationCandidate(uint64_t _random) const;
    // Every bucket entry (no replacements).
    std::vector<NodeEntry> all() const;

    size_t size() const { return m_size; }
    size_t bucketSize(size_t _bucket) const { return m_buckets[_bucket].entries.size(); }
    bcos::h256 const& selfId() const { return m_selfId; }

private:
    struct Bucket
    {
        std::vector<NodeEntry> entries;       // head = least recently seen
        std::vector<NodeEntry> replacements;  // tail = newest
    };
    size_t bucketIndexOf(bcos::h256 const& _id) const;

    bcos::h256 m_selfId;
    std::array<Bucket, c_bucketCount> m_buckets{};
    size_t m_size{0};
};
}  // namespace bcos::devp2p::disc
