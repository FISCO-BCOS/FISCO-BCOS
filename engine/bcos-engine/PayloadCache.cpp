/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "PayloadCache.h"

#include <algorithm>

namespace bcos::engine
{

PayloadCache::PutResult PayloadCache::put(PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry)
{
    auto entries = m_entries;
    auto hashToId = m_hashToId;
    auto order = m_order;
    PutResult result;

    const bool isNew = !entries.contains(id);
    hashToId[blockHash] = id;
    entries.insert_or_assign(id, std::move(entry));
    if (isNew)
    {
        order.push_back(id);
    }
    while (order.size() > c_maxEntries)
    {
        auto evicted = std::move(order.front());
        order.pop_front();
        entries.erase(evicted);
        std::erase_if(hashToId, [&](const auto& item) { return item.second == evicted; });
        result.evicted.push_back(std::move(evicted));
    }

    m_entries.swap(entries);
    m_hashToId.swap(hashToId);
    m_order.swap(order);
    return result;
}

CommonPayloadEntryPtr PayloadCache::find(const PayloadID& id) const
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

void PayloadCache::retainOnly(const PayloadID& id, const h256& blockHash)
{
    auto entries = m_entries;
    auto hashToId = m_hashToId;
    auto order = m_order;

    const auto entryIt = entries.find(id);
    if (entryIt == entries.end())
    {
        return;
    }
    const auto retainedEntry = entryIt->second;

    entries.clear();
    hashToId.clear();
    order.clear();

    entries.emplace(id, retainedEntry);
    hashToId.emplace(blockHash, id);
    order.push_back(id);

    m_entries.swap(entries);
    m_hashToId.swap(hashToId);
    m_order.swap(order);
}

}  // namespace bcos::engine
