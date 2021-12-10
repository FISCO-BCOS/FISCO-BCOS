/*
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
 * @brief implementation of clock cache, which used to cache deserialization result of ABI
 * info
 * @file ClockCache.cpp
 * @author: catli
 * @date: 2021-09-11
 */

#include "ClockCache.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

CacheItem* CacheShard::insert(size_t hash, void* value, bool holdReference)
{
    auto guard = lock_guard<mutex>(m_mutex);
    auto success = evictFromCache();
    if (!success)
    {
        return nullptr;
    }

    // Grab available slot from recycle bin. If recycle bin is empty, create
    // and append new slot to end of circular list.
    CacheItem* item = nullptr;
    if (!m_recycle.empty())
    {
        item = m_recycle.back();
        m_recycle.pop_back();
    }
    else
    {
        m_list.emplace_back();
        item = &m_list.back();
    }

    item->hash = hash;
    item->value = value;
    auto flags = holdReference ? s_inCacheBit + s_oneRef : s_inCacheBit;

    item->flags.store(flags, std::memory_order_relaxed);
    HashTable::accessor accessor;
    if (m_table.find(accessor, hash))
    {
        auto existingHandle = accessor->second;
        m_table.erase(accessor);
        unsetInCache(existingHandle);
    }
    m_table.insert(HashTable::value_type(hash, item));
    m_usage.fetch_add(1, std::memory_order_relaxed);
    return item;
}

CacheItem* CacheShard::lookup(size_t hash)
{
    HashTable::const_accessor accessor;
    if (!m_table.find(accessor, hash))
    {
        return nullptr;
    }

    CacheItem* item = accessor->second;
    accessor.release();
    // ref() could fail if another thread sneak in and evict/erase the cache
    // entry before we are able to hold reference.
    if (!ref(item))
    {
        return nullptr;
    }
    else

        // Double check the key since the item may now representing another key
        // if other threads sneak in, evict/erase the entry and re-used the item
        // for another cache entry.
        if (hash != item->hash)
    {
        unref(item, false);
        return nullptr;
    }
    return item;
}

bool CacheShard::ref(CacheItem* item)
{
    // CAS loop to increase reference count.
    uint32_t flags = item->flags.load(std::memory_order_relaxed);
    while (inCache(flags))
    {
        // Use acquire semantics on success, as further operations on the cache
        // entry has to be order after reference count is increased.
        if (item->flags.compare_exchange_weak(
                flags, flags + s_oneRef, std::memory_order_acquire, std::memory_order_relaxed))
        {
            return true;
        }
    }
    return false;
}

bool CacheShard::unref(CacheItem* item, bool setUsage)
{
    if (setUsage)
    {
        item->flags.fetch_or(s_usageBit, std::memory_order_relaxed);
    }
    // Use acquire-release semantics as previous operations on the cache entry
    // has to be order before reference count is decreased, and potential cleanup
    // of the entry has to be order after.
    uint32_t flags = item->flags.fetch_sub(s_oneRef, std::memory_order_acq_rel);
    assert(refCounts(flags) > 0);
    if (refCounts(flags) == 1)
    {
        if (!inCache(flags))
        {
            auto guard = lock_guard<mutex>(m_mutex);
            recycleItem(item);
            return true;
        }
    }
    return false;
}

void CacheShard::unsetInCache(CacheItem* item)
{
    assert(!m_mutex.try_lock());

    // Use acquire-release semantics as previous operations on the cache entry
    // has to be order before reference count is decreased, and potential cleanup
    // of the entry has to be order after.
    uint32_t flags = item->flags.fetch_and(~s_inCacheBit, std::memory_order_acq_rel);
    // Cleanup if it is the last reference.
    if (inCache(flags) && refCounts(flags) == 0)
    {
        recycleItem(item);
    }
}

bool CacheShard::evictFromCache()
{
    assert(!m_mutex.try_lock());
    auto usage = m_usage.load(std::memory_order_relaxed);
    auto capacity = m_capacity.load(memory_order_relaxed);
    if (usage == 0)
    {
        return true;
    }

    auto newHead = m_head;
    bool is2ndIteration = false;
    while (usage >= capacity)
    {
        assert(newHead < m_list.size());
        auto evicted = tryEvict(&m_list[newHead]);
        newHead = (newHead + 1 >= m_list.size()) ? 0 : newHead + 1;
        if (evicted)
        {
            usage = m_usage.load(memory_order_relaxed);
        }
        else
        {
            if (newHead == m_head)
            {
                if (is2ndIteration)
                {
                    return false;
                }
                else
                {
                    is2ndIteration = true;
                }
            }
        }
    }
    m_head = newHead;
    return true;
}

bool CacheShard::tryEvict(CacheItem* item)
{
    assert(!m_mutex.try_lock());
    auto flags = s_inCacheBit;
    if (item->flags.compare_exchange_strong(
            flags, 0, std::memory_order_acquire, std::memory_order_relaxed))
    {
        auto erased = m_table.erase(item->hash);
        assert(erased);
        recycleItem(item);
        return true;
    }
    item->flags.fetch_and(~s_usageBit, memory_order_relaxed);
    return false;
}

void CacheShard::recycleItem(CacheItem* item)
{
    assert(!m_mutex.try_lock());
    auto& flags = item->flags;
    assert(!inCache(flags) && refCounts(flags) == 0);
    m_deleter(item->value);
    m_recycle.push_back(item);
    m_usage.fetch_sub(1, std::memory_order_relaxed);
}

void CacheShard::setCapacity(size_t capacity)
{
    assert(capacity > 0);
    auto guard = lock_guard<mutex>(m_mutex);
    m_capacity.store(capacity, std::memory_order_relaxed);
    evictFromCache();
}

CacheShard::~CacheShard()
{
    for (auto& item : m_list)
    {
        uint32_t flags = item.flags.load(std::memory_order_relaxed);
        if (inCache(flags) || refCounts(flags) > 0)
        {
            m_deleter(item.value);
        }
    }
}
