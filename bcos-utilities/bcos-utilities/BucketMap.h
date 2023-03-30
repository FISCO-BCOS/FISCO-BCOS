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
#include <queue>
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

        WriteAccessor(Bucket::Ptr bucket, WriteGuard guard)
          : m_bucket(std::move(bucket)), m_writeGuard(std::move(guard))
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
        ReadAccessor(Bucket::Ptr bucket, ReadGuard guard)
          : m_bucket(std::move(bucket)), m_readGuard(std::move(guard))
        {}

        void setValue(typename MapType::iterator it) { m_it = it; };

        const KeyType& key() { return m_it->first; }
        const ValueType& value() { return m_it->second; }


    private:
        typename Bucket::Ptr m_bucket;
        typename MapType::iterator m_it;
        ReadGuard m_readGuard;
    };

    // return true if the lock has acquired
    bool acquireAccessor(typename WriteAccessor::Ptr& accessor, bool wait)
    {
        WriteGuard guard = wait ? WriteGuard(m_mutex) : WriteGuard(m_mutex, boost::try_to_lock);
        if (guard.owns_lock())
        {
            accessor = std::make_shared<WriteAccessor>(
                this->shared_from_this(), std::move(guard));  // acquire lock here
            return true;
        }
        else
        {
            return false;
        }
    }

    // return true if the lock has acquired
    bool acquireAccessor(typename ReadAccessor::Ptr& accessor, bool wait)
    {
        ReadGuard guard = wait ? ReadGuard(m_mutex) : ReadGuard(m_mutex, boost::try_to_lock);
        if (guard.owns_lock())
        {
            accessor = std::make_shared<ReadAccessor>(
                this->shared_from_this(), std::move(guard));  // acquire lock here
            return true;
        }
        else
        {
            return false;
        }
    }


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
    bool insert(typename WriteAccessor::Ptr& accessor, const std::pair<KeyType, ValueType>& kv)
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
    std::pair<bool, ValueType> remove(typename WriteAccessor::Ptr& accessor, const KeyType& key)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<WriteAccessor>(this->shared_from_this());  // acquire lock here
        }

        auto it = m_values.find(key);
        if (it == m_values.end())
        {
            return {false, {}};
        }
        else
        {
            auto value = std::move(it->second);
            m_values.erase(it);
            return {true, std::move(value)};
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
    bool forEach(std::function<bool(typename AccessorType::Ptr)> handler,
        typename AccessorType::Ptr accessor = nullptr)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<AccessorType>(this->shared_from_this());  // acquire lock here
        }

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
        }

        m_values.clear();
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
        auto bucket = m_buckets[idx];
        return bucket->template find<AccessorType>(accessor, key);
    }

    // handler: accessor is nullptr if not found, handler return false to break to find
    template <class AccessorType>
    void batchFind(
        const auto& keys, std::function<bool(const KeyType&, typename AccessorType::Ptr)> handler)
    {
        forEach<AccessorType>(keys, [handler = std::move(handler)](const KeyType& key,
                                        typename Bucket<KeyType, ValueType>::Ptr bucket,
                                        typename AccessorType::Ptr accessor) {
            bucket->template find<AccessorType>(accessor, key);
            return handler(key, accessor);
        });
    }

    void batchInsert(const auto& kvs,
        std::function<void(bool, const KeyType&, typename WriteAccessor::Ptr)> onInsert)
    {
        forEach<WriteAccessor>(kvs, [onInsert = std::move(onInsert)](decltype(kvs.front()) kv,
                                        typename Bucket<KeyType, ValueType>::Ptr bucket,
                                        typename WriteAccessor::Ptr accessor) {
            bucket->insert(accessor, kv);
            return true;
        });
    }

    void batchInsert(const auto& kvs)
    {
        batchInsert(kvs, [](bool, const KeyType&, typename WriteAccessor::Ptr) {});
    }

    void batchRemove(
        const auto& keys, std::function<void(bool, const KeyType&, const ValueType&)> onRemove)
    {
        forEach<WriteAccessor>(keys, [onRemove = std::move(onRemove)](const KeyType& key,
                                         typename Bucket<KeyType, ValueType>::Ptr bucket,
                                         typename WriteAccessor::Ptr accessor) {
            auto [success, value] = bucket->remove(accessor, key);
            onRemove(success, key, value);
            return true;
        });
    }

    void batchRemove(const auto& keys)
    {
        batchRemove(keys, [](bool, const KeyType&, const ValueType&) {});
    }

    bool insert(typename WriteAccessor::Ptr& accessor, std::pair<KeyType, ValueType> kv)
    {
        auto idx = getBucketIndex(kv.first);
        auto bucket = m_buckets[idx];
        return bucket->insert(accessor, std::move(kv));
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

    void clear(std::function<void(bool, const KeyType&, const ValueType&)> onRemove = nullptr)
    {
        if (!onRemove) [[likely]]
        {
            for (size_t i = 0; i < m_buckets.size(); i++)
            {
                m_buckets[i] = std::make_shared<Bucket<KeyType, ValueType>>();
            }
        }
        else
        {
            // idx and bucket
            std::queue<std::pair<size_t, typename Bucket<KeyType, ValueType>::Ptr>> bucket2Remove;
            for (size_t i = 0; i < m_buckets.size(); i++)
            {
                const auto& bucket = m_buckets[i];
                bucket2Remove.emplace(i, std::move(bucket));
                m_buckets[i] = std::make_shared<Bucket<KeyType, ValueType>>();
            }

            while (!bucket2Remove.empty())
            {
                auto it = std::move(bucket2Remove.front());
                auto idx = it.first;
                auto& bucket = it.second;
                bucket2Remove.pop();
                typename ReadAccessor::Ptr accessor;
                bool needWait = bucket2Remove.empty();  // wait if is last bucket
                bool acquired = bucket->acquireAccessor(accessor, needWait);
                if (acquired) [[likely]]
                {
                    bucket->template forEach<ReadAccessor>(
                        [&](typename ReadAccessor::Ptr accessor) {
                            onRemove(true, accessor->key(), accessor->value());
                            return true;
                        },
                        accessor);
                }
                else
                {
                    bucket2Remove.template emplace(std::move(it));
                }
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
        size_t bucketsSize = m_buckets.size();

        auto indexes =
            RANGES::iota_view<size_t, size_t>{startIdx, startIdx + bucketsSize} |
            RANGES::views::transform([bucketsSize](size_t i) { return i % bucketsSize; });

        forEachBucket<AccessorType>(indexes,
            [handler = std::move(handler)](size_t, typename Bucket<KeyType, ValueType>::Ptr bucket,
                typename AccessorType::Ptr accessor) {
                return bucket->template forEach<AccessorType>(handler, accessor);
            });
    }

    template <class AccessorType>  // handler return isContinue
    void forEachBucket(const auto& bucketIndexes,
        std::function<bool(
            size_t idx, typename Bucket<KeyType, ValueType>::Ptr, typename AccessorType::Ptr)>
            handler)
    {
        // idx and bucket
        std::queue<std::pair<size_t, typename Bucket<KeyType, ValueType>::Ptr>> bucket2Process;

        for (size_t idx : bucketIndexes)
        {
            bucket2Process.template emplace(idx, m_buckets[idx]);
        }

        while (!bucket2Process.empty())
        {
            auto it = std::move(bucket2Process.front());
            auto idx = it.first;
            auto& bucket = it.second;
            bucket2Process.pop();
            typename AccessorType::Ptr accessor;
            bool needWait = bucket2Process.empty();  // wait if is last bucket
            bool acquired = bucket->acquireAccessor(accessor, needWait);
            if (acquired) [[likely]]
            {
                if (!handler(idx, bucket, accessor))
                {
                    break;
                }
            }
            else
            {
                bucket2Process.template emplace(std::move(it));
            }
        }
    }

    template <class AccessorType>  // handler return isContinue
    void forEach(const auto& objs,
        std::function<bool(decltype(objs.front()), typename Bucket<KeyType, ValueType>::Ptr,
            typename AccessorType::Ptr)>
            handler)
    {
        auto batches = objs | RANGES::views::chunk_by([this](const auto& a, const auto& b) {
            return this->getBucketIndex(a) == this->getBucketIndex(b);
        });

        std::queue<
            std::tuple<size_t, decltype(batches.front()), typename Bucket<KeyType, ValueType>::Ptr>>
            bucket2Process;

        for (const auto& batch : batches)
        {
            typename AccessorType::Ptr accessor;
            size_t idx = getBucketIndex(batch.front());
            auto bucket = m_buckets[idx];
            bool acquired = bucket->acquireAccessor(accessor, false);

            if (acquired) [[likely]]
            {
                for (const auto& obj : batch)
                {
                    if (!handler(obj, bucket, accessor))
                    {
                        return;
                    }
                }
            }
            else
            {
                bucket2Process.template emplace(idx, batch, bucket);
            }
        }

        while (!bucket2Process.empty())
        {
            auto it = std::move(bucket2Process.front());
            auto idx = std::get<0>(it);
            const auto& batch = std::get<1>(it);
            auto& bucket = std::get<2>(it);
            bucket2Process.pop();

            typename AccessorType::Ptr accessor;
            bool needWait = bucket2Process.empty();  // wait if is last bucket
            bool acquired = bucket->acquireAccessor(accessor, bucket2Process.empty());

            if (acquired) [[likely]]
            {
                for (const auto& obj : batch)
                {
                    if (!handler(obj, bucket, accessor))
                    {
                        return;
                    }
                }
            }
            else
            {
                bucket2Process.template emplace(std::move(it));
            }
        }
    }

protected:
    size_t getBucketIndex(const std::pair<const KeyType&, const ValueType&>& kv)
    {
        return getBucketIndex(kv.first);
    }

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

    using WriteAccessor = typename BucketMap<KeyType, EmptyType, BucketHasher>::WriteAccessor;
    using ReadAccessor = typename BucketMap<KeyType, EmptyType, BucketHasher>::ReadAccessor;

    bool insert(typename BucketSet::WriteAccessor::Ptr& accessor, KeyType key)
    {
        return BucketMap<KeyType, EmptyType, BucketHasher>::insert(
            accessor, {std::move(key), EmptyType()});
    }

    void batchInsert(const auto& keys,
        std::function<void(bool, const KeyType&, typename BucketSet::WriteAccessor::Ptr)> onInsert)
    {
        BucketSet::template forEach<typename BucketSet::WriteAccessor>(
            keys, [onInsert = std::move(onInsert)](const KeyType& key,
                      typename Bucket<KeyType, EmptyType>::Ptr bucket,
                      typename BucketSet::WriteAccessor::Ptr accessor) {
                bool success = bucket->insert(accessor, {key, EmptyType()});
                onInsert(success, key, success ? accessor : nullptr);
                return true;
            });
    }

    void batchInsert(const auto& keys)
    {
        batchInsert(keys, [](bool, const KeyType&, typename BucketSet::WriteAccessor::Ptr) {});
    }
};

}  // namespace bcos
