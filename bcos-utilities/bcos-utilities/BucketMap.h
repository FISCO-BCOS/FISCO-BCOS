/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file BucketMap.h
 * @author: jimmyshi
 * @date 2023/3/3
 */
#pragma once

#include "Common.h"
#include "Ranges.h"
#include <map>
#include <range/v3/view/group_by.hpp>
#include <unordered_map>
#include <vector>

namespace bcos
{

class EmptyType
{
};

template <class KeyType, class ValueType>
class Bucket : public std::enable_shared_from_this<Bucket<KeyType, ValueType>>
{
public:
    using Ptr = std::shared_ptr<Bucket>;
    using MapType = std::unordered_map<KeyType, ValueType>;

    Bucket() = default;
    Bucket(const Bucket&) = default;
    Bucket(Bucket&&) = default;
    Bucket& operator=(const Bucket&) = default;
    Bucket& operator=(Bucket&&) noexcept = default;

    class WriteAccessor
    {
    public:
        using Ptr = std::shared_ptr<WriteAccessor>;

        WriteAccessor(Bucket::Ptr bucket)
          : m_bucket(std::move(bucket)), m_writeGuard(m_bucket->getMutex())
        {}
        void setValue(typename MapType::iterator it) { m_it = it; };

        const KeyType& key() { return m_it->first; }
        ValueType& value() { return m_it->second; }

    private:
        typename Bucket::Ptr m_bucket;
        typename MapType::iterator m_it;
        WriteGuard m_writeGuard;
    };

    class ReadAccessor
    {
    public:
        using Ptr = std::shared_ptr<ReadAccessor>;

        ReadAccessor(Bucket::Ptr bucket)
          : m_bucket(std::move(bucket)), m_readGuard(m_bucket->getMutex())
        {}
        void setValue(typename MapType::iterator it) { m_it = it; };

        const KeyType& key() { return m_it->first; }
        const ValueType& value() { return m_it->second; }


    private:
        typename Bucket::Ptr m_bucket;
        typename MapType::iterator m_it;
        ReadGuard m_readGuard;
    };

    // return true if found
    template <class AccessorType>
    bool find(typename AccessorType::Ptr& accessor, const KeyType& key)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<AccessorType>(this->shared_from_this());  // acquire lock here
        }

        auto it = m_values.find(key);
        if (it == m_values.end())
        {
            accessor = nullptr;
            return false;
        }
        else
        {
            accessor->setValue(it);
            return true;
        }
    }

    // return true if insert happen
    bool insert(typename WriteAccessor::Ptr& accessor, std::pair<KeyType, ValueType> kv)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<WriteAccessor>(this->shared_from_this());  // acquire lock here
        }
        auto [it, inserted] = m_values.try_emplace(kv.first, kv.second);
        accessor->setValue(it);
        return inserted;
    }

    // return {} if not exists before remove
    ValueType remove(const KeyType& key)
    {
        bcos::WriteGuard guard(m_mutex);

        auto it = m_values.find(key);
        if (it == m_values.end())
        {
            return {};
        }

        ValueType ret = std::move(it->second);
        m_values.erase(it);
        return ret;
    }

    // return true if remove success
    bool remove(typename WriteAccessor::Ptr& accessor, const KeyType& key)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<WriteAccessor>(this->shared_from_this());  // acquire lock here
        }

        auto it = m_values.find(key);
        if (it == m_values.end())
        {
            accessor->setValue(m_values.end());
            return false;
        }
        else
        {
            accessor->setValue(it);
            m_values.erase(it);
            return true;
        }
    }

    size_t size() { return m_values.size(); }
    bool contains(const KeyType& key)
    {
        ReadGuard guard(m_mutex);
        return m_values.contains(key);
    }

    // return true if need continue
    template <class AccessorType>  // handler return isContinue
    bool forEach(std::function<bool(typename AccessorType::Ptr)> handler)
    {
        typename AccessorType::Ptr accessor =
            std::make_shared<AccessorType>(this->shared_from_this());  // acquire lock here
        for (auto it = m_values.begin(); it != m_values.end(); it++)
        {
            accessor->setValue(it);
            if (!handler(accessor))
            {
                return false;
            }
        }
        return true;
    }

    void clear(typename WriteAccessor::Ptr& accessor,
        std::function<void(bool, const KeyType&, typename WriteAccessor::Ptr)> onRemove = nullptr)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<WriteAccessor>(this->shared_from_this());  // acquire lock here
        }

        for (auto it = m_values.begin(); it != m_values.end(); it++)
        {
            if (onRemove)
            {
                onRemove(true, it->first, accessor);
            }
            m_values.erase(it);
        }
    }

    SharedMutex& getMutex() { return m_mutex; }

private:
    MapType m_values;
    mutable SharedMutex m_mutex;
};

template <class KeyType, class ValueType, class BucketHasher = std::hash<KeyType>>
class BucketMap
{
public:
    using Ptr = std::shared_ptr<BucketMap>;
    using WriteAccessor = typename Bucket<KeyType, ValueType>::WriteAccessor;
    using ReadAccessor = typename Bucket<KeyType, ValueType>::ReadAccessor;

    BucketMap(size_t bucketSize)
    {
        for (size_t i = 0; i < bucketSize; i++)
        {
            m_buckets.push_back(std::make_shared<Bucket<KeyType, ValueType>>());
        }
    }

    virtual ~BucketMap(){};
    // return true if found
    template <class AccessorType>
    bool find(typename AccessorType::Ptr& accessor, const KeyType& key)
    {
        auto idx = getBucketIndex(key);
        return m_buckets[idx]->template find<AccessorType>(accessor, key);
    }

    // handler: accessor is nullptr if not found, handler return false to break to find
    template <class AccessorType>
    void batchFind(
        const auto& keys, std::function<bool(const KeyType&, typename AccessorType::Ptr)> handler)
    {
        auto keyBatches =
            keys | RANGES::views::chunk_by([this](const KeyType& a, const KeyType& b) {
                return getBucketIndex(a) == getBucketIndex(b);
            });

        for (const auto& keyBatch : keyBatches)
        {
            typename AccessorType::Ptr accessor;
            auto idx = getBucketIndex(*keyBatch.begin());
            for (const auto& key : keyBatch)
            {
                m_buckets[idx]->template find<AccessorType>(accessor, key);
                if (!handler(key, accessor))
                {
                    break;
                }
            }
        }
    }

    void batchInsert(const auto& kvs,
        std::function<void(bool, const KeyType&, typename WriteAccessor::Ptr)> onInsert)
    {
        auto kvsBatches =
            kvs | RANGES::views::chunk_by([this](const std::pair<KeyType, ValueType>& kva,
                                              const std::pair<KeyType, ValueType>& kvb) {
                return getBucketIndex(kva.first) == getBucketIndex(kvb.first);
            });

        for (const auto& kvBatch : kvsBatches)
        {
            typename WriteAccessor::Ptr accessor;
            auto idx = getBucketIndex(*kvBatch.begin());
            for (const auto& kv : kvBatch)
            {
                bool success = m_buckets[idx]->insert(accessor, std::move(kv));
                onInsert(success, kv.first, success ? accessor : nullptr);
            }
        }
    }

    void batchRemove(const auto& keys,
        std::function<void(bool, const KeyType&, typename WriteAccessor::Ptr)> onRemove)
    {
        auto keyBatches =
            keys | RANGES::views::chunk_by([this](const KeyType& a, const KeyType& b) {
                return getBucketIndex(a) == getBucketIndex(b);
            });

        for (const auto& keyBatch : keyBatches)
        {
            typename WriteAccessor::Ptr accessor;
            auto idx = getBucketIndex(*keyBatch.begin());
            for (const auto& key : keyBatch)
            {
                bool success = m_buckets[idx]->remove(accessor, key);
                onRemove(success, key, success ? accessor : nullptr);
            }
        }
    }

    void batchRemove(const auto& keys)
    {
        batchRemove(keys, [](bool, const KeyType&, typename WriteAccessor::Ptr) {});
    }


    bool insert(typename WriteAccessor::Ptr& accessor, std::pair<KeyType, ValueType> kv)
    {
        auto idx = getBucketIndex(kv.first);
        return m_buckets[idx]->insert(accessor, std::move(kv));
    }

    ValueType remove(const KeyType& key)
    {
        auto idx = getBucketIndex(key);
        auto bucket = m_buckets[idx];
        return bucket->remove(key);
    }

    size_t size() const
    {
        size_t size = 0;
        for (auto& bucket : m_buckets)
        {
            size += bucket->size();
        }
        return size;
    }

    bool empty() const { return size() == 0; }

    bool contains(const KeyType& key)
    {
        auto idx = getBucketIndex(key);
        auto bucket = m_buckets[idx];
        return bucket->contains(key);
    }

    void clear(
        std::function<void(bool, const KeyType&, typename WriteAccessor::Ptr)> onRemove = nullptr)
    {
        if (!onRemove) [[likely]]
        {
            for (size_t i = 0; i < m_buckets.size(); i++)
            {
                m_buckets[i] = std::make_shared<Bucket<KeyType, ValueType>>();
                /*
                typename WriteAccessor::Ptr accessor;
                m_buckets[i]->clear(accessor);
                 */
            }
        }
        else
        {
            for (size_t i = 0; i < m_buckets.size(); i++)
            {
                auto bucket = m_buckets[i];
                m_buckets[i] = std::make_shared<Bucket<KeyType, ValueType>>();

                typename WriteAccessor::Ptr accessor;
                bucket->clear(accessor, onRemove);
            }
        }
    }
    template <class AccessorType>  // handler return isContinue
    void forEach(std::function<bool(typename AccessorType::Ptr)> handler)
    {
        forEach<AccessorType>(std::rand() % m_buckets.size(), std::move(handler));
    }

    template <class AccessorType>  // handler return isContinue
    void forEach(const KeyType& startAfter, std::function<bool(typename AccessorType::Ptr)> handler)
    {
        size_t startIdx = (getBucketIndex(startAfter) + 1) % m_buckets.size();
        forEach<AccessorType>(startIdx, std::move(handler));
    }

    template <class AccessorType>  // handler return isContinue
    void forEach(size_t startIdx, std::function<bool(typename AccessorType::Ptr)> handler)
    {
        size_t x = startIdx;

        size_t limit = m_buckets.size();
        while (limit-- > 0)
        {
            auto idx = x++ % m_buckets.size();
            if (!m_buckets[idx]->template forEach<AccessorType>(handler))
            {
                break;
            }
        }
    }

protected:
    size_t getBucketIndex(const KeyType& key)
    {
        auto hash = BucketHasher{}(key);
        return hash % m_buckets.size();
    }

    std::vector<typename Bucket<KeyType, ValueType>::Ptr> m_buckets;
};

template <class KeyType, class BucketHasher = std::hash<KeyType>>
class BucketSet : public BucketMap<KeyType, EmptyType, BucketHasher>
{
public:
    BucketSet(size_t bucketSize) : BucketMap<KeyType, EmptyType, BucketHasher>(bucketSize){};
    ~BucketSet() override = default;

    bool insert(typename BucketSet::WriteAccessor::Ptr& accessor, KeyType key)
    {
        return BucketMap<KeyType, EmptyType, BucketHasher>::insert(
            accessor, {std::move(key), EmptyType()});
    }

    void batchInsert(const auto& keys,
        std::function<void(bool, const KeyType&, typename BucketSet::WriteAccessor::Ptr)> onInsert)
    {
        auto keyBatches =
            keys | RANGES::views::chunk_by([this](const KeyType& a, const KeyType& b) {
                return BucketMap<KeyType, EmptyType, BucketHasher>::getBucketIndex(a) ==
                       BucketMap<KeyType, EmptyType, BucketHasher>::getBucketIndex(b);
            });

        for (const auto& keyBatch : keyBatches)
        {
            typename BucketSet::WriteAccessor::Ptr accessor;
            for (const auto& key : keyBatch)
            {
                bool success = insert(accessor, key);
                onInsert(success, key, success ? accessor : nullptr);
            }
        }
    }
};

}  // namespace bcos