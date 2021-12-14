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
 * @file ClockCache.h
 * @author: catli
 * @date: 2021-09-11
 */

#pragma once

#include "Abi.h"
#include "libutilities/Common.h"
#include <tbb/concurrent_hash_map.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace bcos
{
namespace executor
{
// Cache entry meta data.
struct CacheItem
{
    CacheItem() = default;

    CacheItem(const CacheItem& a) { *this = a; }

    CacheItem& operator=(const CacheItem& a)
    {
        value = a.value;
        return *this;
    }

    size_t hash;

    void* value;

    // Flags and counters associated with the cache item:
    //   lowest bit: in-cache bit
    //   second lowest bit: usage bit
    //   the rest bits: reference count
    // The item is unused when flags equals to 0. The thread decreases the count
    // to 0 is responsible to put the item back to recycle and cleanup memory.
    std::atomic<uint32_t> flags;
};

// A cache shard which maintains its own clock cache.
class CacheShard
{
public:
    using HashTable = tbb::concurrent_hash_map<uint32_t, CacheItem*>;
    using Deleter = void (*)(void* value);

    CacheShard() : m_head(0), m_usage(0) {}

    // Insert a mapping from key->value into the cache.
    CacheItem* insert(size_t hash, void* value, bool holdReference);

    // If the cache has no mapping for "key", returns nullptr, otherwise return a
    // item that corresponds to the mapping. The caller must call this->unref(item)
    // when the returned mapping is no longer needed.
    CacheItem* lookup(size_t hash);

    // Increments the reference count for the item if it refers to an entry in
    // the cache. Returns true if refcount was incremented; otherwise, returns
    // false.
    //
    // REQUIRES: item must have been returned by a method on *this.
    bool ref(CacheItem* item);

    // Decrease reference count of the entry. If this decreases the count to 0,
    // recycle the entry. If set_usage is true, also set the usage bit. Returns true
    // if a value is erased.
    //
    // Not necessary to hold mutex_ before being called.
    bool unref(CacheItem* item, bool setUsage);

    void setCapacity(size_t capacity);

    void setDeleter(Deleter deleter) { m_deleter = deleter; }

    ~CacheShard();

private:
    static const std::uint32_t s_inCacheBit = 1;
    static const std::uint32_t s_usageBit = 2;
    static const std::uint32_t s_refCountOffset = 2;
    static const std::uint32_t s_oneRef = 1 << s_refCountOffset;

    static bool inCache(uint32_t flags) { return flags & s_inCacheBit; }
    static bool hasUsage(uint32_t flags) { return flags & s_usageBit; }
    static uint32_t refCounts(uint32_t flags) { return flags >> s_refCountOffset; }

    // Unset in-cache bit of the entry. Recycle the item if necessary, returns
    // true if a value is erased.
    //
    // Has to hold mutex_ before being called.
    void unsetInCache(CacheItem* item);

    // Scan through the circular list, evict entries until we get enough space
    // for a new cache entry. Return true if success, false otherwise.
    //
    // Has to hold mutex_ before being called.
    bool evictFromCache();

    // Examine the item for eviction. If the item is in cache, usage bit is
    // not set, and referece count is 0, evict it from cache. Otherwise unset
    // the usage bit.
    //
    // Has to hold mutex_ before being called.
    bool tryEvict(CacheItem* item);

    // Put the item back to recycle list, and put the value associated with
    // it into to-be-deleted list.
    //
    // Has to hold mutex_ before being called.
    void recycleItem(CacheItem* item);

    // The circular list of cache handles. Initially the list is empty. Once a
    // item is needed by insertion, and no more handles are available in
    // recycle bin, one more item is appended to the end.
    //
    // We use std::deque for the circular list because we want to make sure
    // pointers to handles are valid through out the life-cycle of the cache
    // (in contrast to std::vector), and be able to grow the list (in contrast
    // to statically allocated arrays).
    std::deque<CacheItem> m_list;

    // Pointer to the next item in the circular list to be examine for
    // eviction.
    size_t m_head;

    // Recycle bin of cache handles.
    std::vector<CacheItem*> m_recycle;

    // Maximum cache size.
    std::atomic<size_t> m_capacity;

    // Current total size of the cache.
    std::atomic<size_t> m_usage;

    // Guards m_list, m_head, and m_recycle. In addition, updating m_table also has
    // to hold the mutex, to avoid the cache being in inconsistent state.
    std::mutex m_mutex;

    // Hash table (tbb::concurrent_hash_map) for lookup.
    HashTable m_table;

    Deleter m_deleter;
};

template <typename T>
class CacheHandle
{
public:
    CacheHandle() = default;

    CacheHandle(CacheItem* item, CacheShard* ownedShard)
    {
        m_item = item;
        m_ownedShard = ownedShard;
    }

    CacheHandle(const CacheHandle&) = delete;

    CacheHandle& operator=(const CacheHandle&) = delete;

    CacheHandle(CacheHandle&& a)
    {
        if (isValid())
        {
            m_ownedShard->unref(m_item, true);
        }
        m_item = a.m_item;
        m_ownedShard = a.m_ownedShard;
        a.m_item = nullptr;
        a.m_ownedShard = nullptr;
    }

    CacheHandle& operator=(CacheHandle&& a)
    {
        if (this != &a)
        {
            if (isValid())
            {
                m_ownedShard->unref(m_item, true);
            }
            m_item = a.m_item;
            m_ownedShard = a.m_ownedShard;
            a.m_item = nullptr;
            a.m_ownedShard = nullptr;
        }
        return *this;
    }

    ~CacheHandle()
    {
        if (isValid())
        {
            m_ownedShard->unref(m_item, true);
        }
    }

    T& value() const { return *static_cast<T*>(m_item->value); }

    bool release()
    {
        if (isValid())
        {
            auto result = m_ownedShard->unref(m_item, true);
            m_item = nullptr;
            m_ownedShard = nullptr;
            return result;
        }
        return false;
    }

    bool isValid() const { return m_item != nullptr && m_ownedShard != nullptr; }

private:
    CacheItem* m_item;
    CacheShard* m_ownedShard;
};

template <typename K, typename V>
class ClockCache
{
public:
    ClockCache(size_t capacity, int numShardBits = -1)
    {
        assert(capacity > 0);
        if (numShardBits == -1)
        {
            numShardBits = 4;
        }
        m_numShards = 1 << numShardBits;
        m_shardMask = (size_t{1} << numShardBits) - 1;
        m_shards = new CacheShard[m_numShards];

        for (auto i = 0u; i < m_numShards; ++i)
        {
            m_shards[i].setCapacity(capacity);
            m_shards[i].setDeleter([](void* value) { delete static_cast<V*>(value); });
        }
    }

    CacheHandle<V> lookup(const K& key)
    {
        auto hasher = boost::hash<K>();
        auto hash = hasher(key);
        auto& ownedShard = getShard(hash);
        auto item = ownedShard.lookup(hash);
        return CacheHandle<V>(item, &ownedShard);
    }

    bool insert(const K& key, V* value, CacheHandle<V>* outHandle = nullptr)
    {
        auto hasher = boost::hash<K>();
        auto hash = hasher(key);
        auto& shard = getShard(hash);
        auto item = shard.insert(hash, value, outHandle != nullptr);
        if (outHandle != nullptr)
        {
            if (item != nullptr)
            {
                (*outHandle) = CacheHandle<V>(item, &shard);
            }
        }
        return item != nullptr;
    }

    ~ClockCache() { delete[] m_shards; }

private:
    CacheShard& getShard(size_t hash)
    {
        auto shardId = hash & m_shardMask;
        return m_shards[shardId];
    }

    size_t m_numShards;
    size_t m_shardMask;
    CacheShard* m_shards;
};
}  // namespace executor
}  // namespace bcos