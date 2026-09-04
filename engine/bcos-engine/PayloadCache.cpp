/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "PayloadCache.h"

#include <algorithm>

namespace bcos::engine
{

namespace
{
void eraseHashesForId(auto& hashToId, PayloadID const& id)
{
    std::erase_if(hashToId, [&](auto const& item) { return item.second == id; });
}
}  // namespace

PayloadCache::PutResult PayloadCache::put(
    PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry)
{
    auto entries = m_entries;
    auto hashToId = m_hashToId;
    auto order = m_order;
    PutResult result;

    const bool isNew = !entries.contains(id);
    eraseHashesForId(hashToId, id);
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
        eraseHashesForId(hashToId, evicted);
        result.evicted.push_back(std::move(evicted));
    }

    m_entries.swap(entries);
    m_hashToId.swap(hashToId);
    m_order.swap(order);
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
    auto entries = m_entries;
    auto hashToId = m_hashToId;
    auto order = m_order;
    PutResult putResult;

    const bool isNew = !entries.contains(id);
    eraseHashesForId(hashToId, id);
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
        eraseHashesForId(hashToId, evicted);
        putResult.evicted.push_back(std::move(evicted));
    }

    const auto entryIt = entries.find(id);
    if (entryIt == entries.end())
    {
        return putResult;
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
