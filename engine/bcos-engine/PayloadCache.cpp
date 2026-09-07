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
 * @file PayloadCache.cpp
 * @brief Definitions of the payload cache
 */

#include "PayloadCache.h"

#include <algorithm>
#include <deque>
#include <unordered_map>

namespace bcos::engine
{

namespace
{
void eraseHashesForId(auto& hashToId, PayloadID const& id)
{
    std::erase_if(hashToId, [&](auto const& item) { return item.second == id; });
}

struct StagedCache
{
    std::unordered_map<PayloadID, BuiltPayloadPtr> entries;
    std::unordered_map<h256, PayloadID> hashToId;
    std::deque<PayloadID> order;
};

PayloadCache::PutResult putStaged(StagedCache& staged, PayloadID id, h256 const& blockHash,
    BuiltPayloadPtr entry, std::size_t maxEntries)
{
    PayloadCache::PutResult result;
    const bool isNew = !staged.entries.contains(id);
    eraseHashesForId(staged.hashToId, id);
    staged.hashToId[blockHash] = id;
    staged.entries.insert_or_assign(id, std::move(entry));
    if (isNew)
    {
        staged.order.push_back(id);
    }
    else
    {
        // Re-put (same deterministic id re-published by an identical FCU retry) refreshes
        // the FIFO position: the entry is now the most recently needed, so it must not
        // age out ahead of never-re-published entries (finding F26).
        std::erase(staged.order, id);
        staged.order.push_back(id);
    }
    while (staged.order.size() > maxEntries)
    {
        auto evicted = std::move(staged.order.front());
        staged.order.pop_front();
        staged.entries.erase(evicted);
        eraseHashesForId(staged.hashToId, evicted);
        result.evicted.push_back(std::move(evicted));
    }
    return result;
}

/// Shared retain kernel: keep only `id` (reinstated at `blockHash`), appending every
/// dropped payload id to `dropped` when non-null. A missing `id` leaves the staged
/// state untouched.
void retainStaged(StagedCache& staged, PayloadID const& id, h256 const& blockHash,
    std::vector<PayloadID>* dropped)
{
    const auto entryIt = staged.entries.find(id);
    if (entryIt == staged.entries.end())
    {
        return;
    }
    const auto retainedEntry = entryIt->second;
    for (auto const& [existingId, _] : staged.entries)
    {
        if (existingId != id && dropped != nullptr)
        {
            dropped->push_back(existingId);
        }
    }
    staged.entries.clear();
    staged.hashToId.clear();
    staged.order.clear();
    staged.entries.emplace(id, retainedEntry);
    staged.hashToId.emplace(blockHash, id);
    staged.order.push_back(id);
}
}  // namespace

PayloadCache::PutResult PayloadCache::put(
    PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry)
{
    StagedCache staged{m_entries, m_hashToId, m_order};
    auto result = putStaged(staged, std::move(id), blockHash, std::move(entry), c_maxEntries);
    m_entries.swap(staged.entries);
    m_hashToId.swap(staged.hashToId);
    m_order.swap(staged.order);
    return result;
}

BuiltPayloadPtr PayloadCache::find(const PayloadID& id) const
{
    auto it = m_entries.find(id);
    if (it == m_entries.end())
    {
        return nullptr;
    }
    return it->second;
}

std::optional<PayloadID> PayloadCache::payloadIdForHash(const h256& blockHash) const
{
    auto it = m_hashToId.find(blockHash);
    if (it == m_hashToId.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::optional<bcos::protocol::BlockNumber> PayloadCache::blockNumberForHash(
    const h256& blockHash) const
{
    auto payloadId = payloadIdForHash(blockHash);
    if (!payloadId.has_value())
    {
        return std::nullopt;
    }
    auto entry = find(*payloadId);
    if (!entry)
    {
        return std::nullopt;
    }
    return entry->executionPayload.blockNumber;
}

PayloadCache::PutResult PayloadCache::putAndRetainOnly(
    PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry)
{
    StagedCache staged{m_entries, m_hashToId, m_order};
    PayloadID const retainedId = id;
    auto putResult = putStaged(staged, std::move(id), blockHash, std::move(entry), c_maxEntries);
    retainStaged(staged, retainedId, blockHash, &putResult.evicted);

    m_entries.swap(staged.entries);
    m_hashToId.swap(staged.hashToId);
    m_order.swap(staged.order);
    return putResult;
}

PayloadCache PayloadCache::duplicate() const
{
    PayloadCache copy;
    copy.m_entries = m_entries;
    copy.m_hashToId = m_hashToId;
    copy.m_order = m_order;
    return copy;
}

void PayloadCache::publishFrom(PayloadCache staged) noexcept
{
    m_entries.swap(staged.m_entries);
    m_hashToId.swap(staged.m_hashToId);
    m_order.swap(staged.m_order);
}

void PayloadCache::erase(PayloadID const& id)
{
    if (!m_entries.contains(id))
    {
        return;
    }

    auto entries = m_entries;
    auto hashToId = m_hashToId;
    auto order = m_order;

    entries.erase(id);
    eraseHashesForId(hashToId, id);
    std::erase(order, id);

    m_entries.swap(entries);
    m_hashToId.swap(hashToId);
    m_order.swap(order);
}

void PayloadCache::retainOnly(const PayloadID& id, const h256& blockHash)
{
    StagedCache staged{m_entries, m_hashToId, m_order};
    retainStaged(staged, id, blockHash, /*dropped=*/nullptr);

    m_entries.swap(staged.entries);
    m_hashToId.swap(staged.hashToId);
    m_order.swap(staged.order);
}

}  // namespace bcos::engine
