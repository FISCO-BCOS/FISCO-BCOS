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
 * @file KademliaTable.cpp
 * @brief discv4 routing table.
 * @date 2026/11/3
 */
#include "KademliaTable.h"
#include "../rlpx/Crypto.h"
#include <algorithm>

namespace bcos::devp2p::disc
{
bcos::h256 KademliaTable::nodeIdOf(bytesConstRef _publicKey)
{
    return bcos::h256(rlpx::keccak256(_publicKey));
}

bcos::h256 KademliaTable::xorDistance(bcos::h256 const& _a, bcos::h256 const& _b)
{
    return _a ^ _b;
}

int KademliaTable::logDistance(bcos::h256 const& _a, bcos::h256 const& _b)
{
    auto distance = xorDistance(_a, _b);
    for (size_t i = 0; i < bcos::h256::SIZE; ++i)
    {
        if (distance[i] == 0)
        {
            continue;
        }
        int leading = 0;
        for (uint8_t mask = 0x80; (distance[i] & mask) == 0; mask >>= 1)
        {
            ++leading;
        }
        return static_cast<int>((bcos::h256::SIZE - i) * 8) - leading;
    }
    return 0;
}

size_t KademliaTable::bucketIndexOf(bcos::h256 const& _id) const
{
    return static_cast<size_t>(logDistance(m_selfId, _id)) - 1;
}

KademliaTable::AddResult KademliaTable::add(NodeEntry _entry)
{
    if (_entry.id == bcos::h256{})
    {
        _entry.id = nodeIdOf(bcos::ref(_entry.publicKey));
    }
    if (_entry.id == m_selfId)
    {
        return AddResult::Self;
    }
    if (auto* existing = find(_entry.id); existing != nullptr)
    {
        // Self-attested data only: the node signed its own record / announced its seq.
        existing->enrSeq = std::max(existing->enrSeq, _entry.enrSeq);
        if (_entry.record && (!existing->record || existing->record->seq() <= _entry.record->seq()))
        {
            existing->record = std::move(_entry.record);
        }
        return AddResult::Updated;
    }
    if (_entry.addedAt == NodeEntry::Clock::time_point{})
    {
        _entry.addedAt = NodeEntry::Clock::now();
    }
    auto& bucket = m_buckets[bucketIndexOf(_entry.id)];
    if (bucket.entries.size() < c_bucketSize)
    {
        bucket.entries.push_back(std::move(_entry));
        ++m_size;
        return AddResult::Added;
    }
    if (bucket.replacements.size() >= c_replacementCacheSize)
    {
        bucket.replacements.erase(bucket.replacements.begin());  // drop the oldest
    }
    bucket.replacements.push_back(std::move(_entry));
    return AddResult::Replacement;
}

bool KademliaTable::updateEndpoint(
    bcos::h256 const& _id, bcos::bytes const& _ip, uint16_t _udp, uint16_t _tcp)
{
    auto* node = find(_id);
    if (node == nullptr)
    {
        return false;
    }
    if (node->ip != _ip || node->udp != _udp)
    {
        node->ip = _ip;
        node->udp = _udp;
        node->lastPongReceived = {};
        node->lastPingReceived = {};
    }
    if (_tcp != 0)
    {
        node->tcp = _tcp;
    }
    return true;
}

NodeEntry* KademliaTable::find(bcos::h256 const& _id)
{
    if (_id == m_selfId)
    {
        return nullptr;
    }
    auto& bucket = m_buckets[bucketIndexOf(_id)];
    auto match = [&](NodeEntry const& _entry) { return _entry.id == _id; };
    if (auto it = std::find_if(bucket.entries.begin(), bucket.entries.end(), match);
        it != bucket.entries.end())
    {
        return &*it;
    }
    if (auto it = std::find_if(bucket.replacements.begin(), bucket.replacements.end(), match);
        it != bucket.replacements.end())
    {
        return &*it;
    }
    return nullptr;
}

NodeEntry const* KademliaTable::find(bcos::h256 const& _id) const
{
    return const_cast<KademliaTable*>(this)->find(_id);
}

bool KademliaTable::remove(bcos::h256 const& _id)
{
    if (_id == m_selfId)
    {
        return false;
    }
    auto& bucket = m_buckets[bucketIndexOf(_id)];
    auto match = [&](NodeEntry const& _entry) { return _entry.id == _id; };
    if (auto it = std::find_if(bucket.entries.begin(), bucket.entries.end(), match);
        it != bucket.entries.end())
    {
        bucket.entries.erase(it);
        --m_size;
        if (!bucket.replacements.empty())
        {
            // Promote the newest replacement; it is appended as most recently seen.
            bucket.entries.push_back(std::move(bucket.replacements.back()));
            bucket.replacements.pop_back();
            ++m_size;
        }
        return true;
    }
    if (auto it = std::find_if(bucket.replacements.begin(), bucket.replacements.end(), match);
        it != bucket.replacements.end())
    {
        bucket.replacements.erase(it);
        return true;
    }
    return false;
}

void KademliaTable::touch(bcos::h256 const& _id)
{
    if (_id == m_selfId)
    {
        return;
    }
    auto& entries = m_buckets[bucketIndexOf(_id)].entries;
    auto it = std::find_if(
        entries.begin(), entries.end(), [&](NodeEntry const& _entry) { return _entry.id == _id; });
    if (it != entries.end())
    {
        std::rotate(it, it + 1, entries.end());
    }
}

std::vector<NodeEntry> KademliaTable::closest(bcos::h256 const& _target, size_t _count) const
{
    auto entries = all();
    std::sort(entries.begin(), entries.end(), [&](NodeEntry const& _a, NodeEntry const& _b) {
        return xorDistance(_a.id, _target) < xorDistance(_b.id, _target);
    });
    if (entries.size() > _count)
    {
        entries.resize(_count);
    }
    return entries;
}

std::optional<bcos::h256> KademliaTable::revalidationCandidate(uint64_t _random) const
{
    if (m_size == 0)
    {
        return std::nullopt;
    }
    // Pick a random non-empty bucket, then its least recently seen entry.
    size_t start = static_cast<size_t>(_random % c_bucketCount);
    for (size_t i = 0; i < c_bucketCount; ++i)
    {
        auto const& bucket = m_buckets[(start + i) % c_bucketCount];
        if (!bucket.entries.empty())
        {
            return bucket.entries.front().id;
        }
    }
    return std::nullopt;
}

std::vector<NodeEntry> KademliaTable::all() const
{
    std::vector<NodeEntry> out;
    out.reserve(m_size);
    for (auto const& bucket : m_buckets)
    {
        out.insert(out.end(), bucket.entries.begin(), bucket.entries.end());
    }
    return out;
}
}  // namespace bcos::devp2p::disc
