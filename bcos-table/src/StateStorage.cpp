/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "StateStorage.h"

namespace bcos::storage
{
template <bool enableLRU>
BaseStorage<enableLRU>::BaseStorage(
    std::shared_ptr<StorageInterface> prev, bool setRowWithDirtyFlag)
  : storage::StateStorageInterface(std::move(prev)),
    m_buckets(std::thread::hardware_concurrency() + 1),
    m_setRowWithDirtyFlag(setRowWithDirtyFlag)
{}

template <bool enableLRU>
BaseStorage<enableLRU>::~BaseStorage()
{
    m_recoder.clear();
}

template <bool enableLRU>
void BaseStorage<enableLRU>::asyncGetPrimaryKeys(std::string_view table,
    const std::optional<storage::Condition const>& condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback)
{
    std::map<std::string_view, storage::Entry::Status> localKeys;

    if (m_enableTraverse)
    {
        std::mutex mergeMutex;
        tbb::parallel_for(tbb::blocked_range<size_t>(0U, m_buckets.size()),
            [this, &mergeMutex, &localKeys, &table, &condition](auto const& range) {
                for (auto i = range.begin(); i < range.end(); ++i)
                {
                    auto& bucket = m_buckets[i];
                    std::unique_lock<std::mutex> lock(bucket.mutex);

                    decltype(localKeys) bucketKeys;
                    for (auto& it : bucket.container)
                    {
                        if (it.table == table && (!condition || condition->isValid(it.key)))
                        {
                            bucketKeys.emplace(it.key, it.entry.status());
                        }
                    }

                    std::unique_lock mergeLock(mergeMutex);
                    localKeys.merge(std::move(bucketKeys));
                }
            });
    }

    auto prev = getPrev();
    if (!prev)
    {
        std::vector<std::string> resultKeys;
        for (auto& localIt : localKeys)
        {
            if (localIt.second == Entry::NORMAL || localIt.second == Entry::MODIFIED)
            {
                resultKeys.push_back(std::string(localIt.first));
            }
        }

        _callback(nullptr, std::move(resultKeys));
        return;
    }

    prev->asyncGetPrimaryKeys(table, condition,
        [localKeys = std::move(localKeys), callback = std::move(_callback)](
            auto&& error, auto&& remoteKeys) mutable {
            if (error)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                             "Get primary keys from prev failed!", *error),
                    std::vector<std::string>());
                return;
            }

            for (auto it = remoteKeys.begin(); it != remoteKeys.end();)
            {
                bool deleted = false;

                auto localIt = localKeys.find(*it);
                if (localIt != localKeys.end())
                {
                    if (localIt->second == Entry::DELETED)
                    {
                        it = remoteKeys.erase(it);
                        deleted = true;
                    }

                    localKeys.erase(localIt);
                }

                if (!deleted)
                {
                    ++it;
                }
            }

            for (auto& localIt : localKeys)
            {
                if (localIt.second == Entry::NORMAL || localIt.second == Entry::MODIFIED)
                {
                    remoteKeys.push_back(std::string(localIt.first));
                }
            }

            callback(nullptr, std::forward<decltype(remoteKeys)>(remoteKeys));
        });
}

template <bool enableLRU>
void BaseStorage<enableLRU>::asyncGetRow(std::string_view tableView, std::string_view keyView,
    std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback)
{
    auto [bucket, lock] = getBucket(tableView, keyView);
    boost::ignore_unused(lock);

    auto it = bucket->container.template get<0>().find(std::make_tuple(tableView, keyView));
    if (it != bucket->container.template get<0>().end())
    {
        auto& entry = it->entry;

        if (entry.status() == Entry::DELETED)
        {
            lock.unlock();
            _callback(nullptr, std::nullopt);
        }
        else
        {
            auto optionalEntry = std::make_optional(entry);
            if constexpr (enableLRU)
            {
                updateMRUAndCheck(*bucket, it);
            }

            lock.unlock();
            _callback(nullptr, std::move(optionalEntry));
        }
        return;
    }

    lock.unlock();

    auto prev = getPrev();
    if (prev)
    {
        prev->asyncGetRow(tableView, keyView,
            [this, prev, table = std::string(tableView), key = std::string(keyView), _callback](
                Error::UniquePtr error, std::optional<Entry> entry) {
                if (error)
                {
                    _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                                  "Get row from storage failed!", *error),
                        {});
                    return;
                }

                if (entry)
                {
                    _callback(nullptr,
                        std::make_optional(importExistingEntry(table, key, std::move(*entry))));
                }
                else
                {
                    _callback(nullptr, std::nullopt);
                }
            });
    }
    else
    {
        _callback(nullptr, std::nullopt);
    }
}

template <bool enableLRU>
void BaseStorage<enableLRU>::asyncGetRows(std::string_view tableView,
    ::ranges::any_view<std::string_view,
        ::ranges::category::input | ::ranges::category::random_access |
            ::ranges::category::sized>
        keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
{
    std::vector<std::optional<Entry>> results(keys.size());
    auto missinges = std::tuple<std::vector<std::string_view>,
        std::vector<std::tuple<std::string, size_t>>>();

    std::atomic_ulong existsCount = 0;

    for (auto i = 0U; i < keys.size(); ++i)
    {
        auto [bucket, lock] = getBucket(tableView, keys[i]);
        boost::ignore_unused(lock);

        auto it = bucket->container.find(std::make_tuple(tableView, std::string_view(keys[i])));
        if (it != bucket->container.end())
        {
            auto& entry = it->entry;
            if (entry.status() == Entry::NORMAL || entry.status() == Entry::MODIFIED)
            {
                results[i].emplace(entry);

                if constexpr (enableLRU)
                {
                    updateMRUAndCheck(*bucket, it);
                }
            }
            else
            {
                results[i] = std::nullopt;
            }
            ++existsCount;
        }
        else
        {
            std::get<1>(missinges).emplace_back(std::string(keys[i]), i);
            std::get<0>(missinges).emplace_back(keys[i]);
        }
    }

    auto prev = getPrev();
    if (existsCount < keys.size() && prev)
    {
        prev->asyncGetRows(tableView, std::get<0>(missinges),
            [this, table = std::string(tableView), callback = std::move(_callback),
                missingIndexes = std::move(std::get<1>(missinges)), results = std::move(results)](
                auto&& error, std::vector<std::optional<Entry>>&& entries) mutable {
                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                                 "async get perv rows failed!", *error),
                        std::vector<std::optional<Entry>>());
                    return;
                }

                for (size_t i = 0; i < entries.size(); ++i)
                {
                    auto& entry = entries[i];
                    if (entry)
                    {
                        results[std::get<1>(missingIndexes[i])].emplace(importExistingEntry(table,
                            std::move(std::get<0>(missingIndexes[i])), std::move(*entry)));
                    }
                }

                callback(nullptr, std::move(results));
            });
    }
    else
    {
        _callback(nullptr, std::move(results));
    }
}

template <bool enableLRU>
void BaseStorage<enableLRU>::asyncSetRow(std::string_view tableView, std::string_view keyView,
    Entry entry, std::function<void(Error::UniquePtr)> callback)
{
    if (m_readOnly)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
            StorageError::ReadOnly, "Try to operate a read-only storage"));
        return;
    }

    ssize_t updatedCapacity = entry.size();
    std::optional<Entry> entryOld;

    if (m_setRowWithDirtyFlag && entry.status() == Entry::NORMAL)
    {
        entry.setStatus(Entry::MODIFIED);
    }
    auto [bucket, lock] = getBucket(tableView, keyView);
    auto it = bucket->container.find(std::make_tuple(tableView, keyView));
    if (it != bucket->container.end())
    {
        auto& existsEntry = it->entry;
        entryOld.emplace(std::move(existsEntry));

        updatedCapacity -= entryOld->size();

        bucket->container.modify(it, [&entry](Data& data) { data.entry = std::move(entry); });
    }
    else
    {
        auto [iter, inserted] =
            bucket->container.emplace(Data{std::string(tableView), std::string(keyView), std::move(entry)});
        boost::ignore_unused(inserted);
        it = iter;
    }
    if constexpr (enableLRU)
    {
        updateMRUAndCheck(*bucket, it);
    }

    if (m_recoder.local())
    {
        m_recoder.local()->log(
            Recoder::Change(std::string(tableView), std::string(keyView), std::move(entryOld)));
    }

    bucket->capacity += updatedCapacity;

    lock.unlock();
    callback(nullptr);
}

template <bool enableLRU>
void BaseStorage<enableLRU>::parallelTraverse(bool onlyDirty,
    std::function<bool(const std::string_view& table, const std::string_view& key,
        const Entry& entry)> callback) const
{
    std::lock_guard<std::mutex> lock(x_cacheMutex);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_buckets.size()),
        [this, &onlyDirty, &callback](auto const& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                auto& bucket = m_buckets[i];

                for (auto& it : bucket.container)
                {
                    auto& entry = it.entry;
                    if (!onlyDirty || entry.dirty())
                    {
                        callback(it.table, it.key, entry);
                    }
                }
            }
        });
}

template <bool enableLRU>
void BaseStorage<enableLRU>::merge(bool onlyDirty, const TraverseStorageInterface& source)
{
    if (&source == this)
    {
        STORAGE_LOG(ERROR) << "Can't merge from self!";
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Can't merge from self!"));
    }

    std::atomic_size_t count = 0;
    source.parallelTraverse(
        onlyDirty, [this, &count](const std::string_view& table, const std::string_view& key,
                       const storage::Entry& entry) {
            asyncSetRow(table, key, entry, [](Error::UniquePtr) {});
            ++count;
            return true;
        });

    STORAGE_LOG(INFO) << "Successful merged records" << LOG_KV("count", count);
}

template <bool enableLRU>
crypto::HashType BaseStorage<enableLRU>::hash(
    const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const
{
    bcos::crypto::HashType totalHash;
    auto blockVersion = features.get(ledger::Features::Flag::bugfix_statestorage_hash) ?
                            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION :
                            (uint32_t)bcos::protocol::BlockVersion::V3_0_VERSION;

    std::vector<bcos::crypto::HashType> hashes(m_buckets.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0U, m_buckets.size()), [&, this](auto const& range) {
        for (auto i = range.begin(); i < range.end(); ++i)
        {
            auto& bucket = m_buckets[i];

            bcos::crypto::HashType bucketHash(0);
            for (auto& it : bucket.container)
            {
                auto& entry = it.entry;
                if (entry.dirty())
                {
                    bcos::crypto::HashType entryHash;
                    if (blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
                    {
                        entryHash = entry.hash(it.table, it.key, *hashImpl, blockVersion);
                    }
                    else
                    {
                        entryHash = hashImpl->hash(it.table) ^ hashImpl->hash(it.key) ^
                                    entry.hash(it.table, it.key, *hashImpl, blockVersion);
                    }
                    bucketHash ^= entryHash;
                }
            }

            hashes[i] ^= bucketHash;
        }
    });

    for (auto const& it : hashes)
    {
        totalHash ^= it;
    }

    return totalHash;
}

template <bool enableLRU>
void BaseStorage<enableLRU>::rollback(const Recoder& recoder)
{
    if (m_readOnly)
    {
        return;
    }

    for (const auto& change : recoder)
    {
        ssize_t updateCapacity = 0;
        auto [bucket, lock] = getBucket(change.table, std::string_view(change.key));
        boost::ignore_unused(lock);

        auto it = bucket->container.find(
            std::make_tuple(std::string_view(change.table), std::string_view(change.key)));
        if (change.entry)
        {
            if (it != bucket->container.end())
            {
                if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert exists: " << change.table << " | "
                                       << toHex(change.key) << " | "
                                       << toHex(change.entry->get());
                }

                updateCapacity = change.entry->size() - it->entry.size();

                const auto& rollbackEntry = change.entry;
                bucket->container.modify(
                    it, [&rollbackEntry](Data& data) { data.entry = std::move(*rollbackEntry); });
            }
            else
            {
                if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert deleted: " << change.table << " | "
                                       << toHex(change.key) << " | "
                                       << toHex(change.entry->get());
                }
                updateCapacity = change.entry->size();
                bucket->container.emplace(Data{change.table, change.key, std::move(*(change.entry))});
            }
        }
        else
        {
            if (it != bucket->container.end())
            {
                if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE)
                        << "Revert insert: " << change.table << " | " << toHex(change.key);
                }

                updateCapacity = 0 - it->entry.size();
                bucket->container.erase(it);
            }
            else
            {
                auto message =
                    fmt::format("Not found rollback entry: {}:{}", change.table, change.key);

                BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
            }
        }

        bucket->capacity += updateCapacity;
    }
}

template <bool enableLRU>
void BaseStorage<enableLRU>::setEnableTraverse(bool enableTraverse)
{
    m_enableTraverse = enableTraverse;
}

template <bool enableLRU>
void BaseStorage<enableLRU>::setMaxCapacity(ssize_t capacity)
{
    m_maxCapacity = capacity;
}

template <bool enableLRU>
Entry BaseStorage<enableLRU>::importExistingEntry(
    std::string_view table, std::string_view key, Entry entry)
{
    if (m_readOnly)
    {
        return entry;
    }
    if (x_cacheMutex.try_lock())
    {
        entry.setStatus(Entry::NORMAL);
        auto updateCapacity = entry.size();

        auto [bucket, lock] = getBucket(table, key);
        auto it = bucket->container.find(std::make_tuple(table, key));

        if (it == bucket->container.end())
        {
            it = bucket->container
                     .emplace(Data{std::string(table), std::string(key), std::move(entry)})
                     .first;

            bucket->capacity += updateCapacity;
        }
        else
        {
            STORAGE_LOG(DEBUG) << "Fail import existsing entry, " << table << " | " << toHex(key);
        }
        x_cacheMutex.unlock();
        return it->entry;
    }
    return entry;
}

template <bool enableLRU>
std::shared_ptr<StorageInterface> BaseStorage<enableLRU>::getPrev()
{
    std::shared_lock<std::shared_mutex> lock(m_prevMutex);
    auto prev = m_prev;
    return prev;
}

template <bool enableLRU>
std::tuple<typename BaseStorage<enableLRU>::Bucket*, std::unique_lock<std::mutex>>
BaseStorage<enableLRU>::getBucket(const std::string_view& table, const std::string_view& key)
{
    auto hash = std::hash<std::string_view>{}(table);
    boost::hash_combine(hash, std::hash<std::string_view>{}(key));
    auto index = hash % m_buckets.size();

    auto& bucket = m_buckets[index];
    return std::make_tuple(&bucket, std::unique_lock<std::mutex>(bucket.mutex));
}

template <bool enableLRU>
void BaseStorage<enableLRU>::updateMRUAndCheck(Bucket& bucket, typename Container::iterator it)
{
    if constexpr (enableLRU)
    {
        auto seqIt = bucket.container.template get<1>().iterator_to(*it);
        bucket.container.template get<1>().relocate(bucket.container.template get<1>().end(), seqIt);

        size_t clearCount = 0;
        while (bucket.capacity > m_maxCapacity && !bucket.container.empty())
        {
            auto& item = bucket.container.template get<1>().front();
            bucket.capacity -= item.entry.size();

            bucket.container.template get<1>().pop_front();
            ++clearCount;
        }

        if (clearCount > 0)
        {
            STORAGE_LOG(TRACE) << "LRUStorage cleared:" << clearCount
                               << ", current size: " << bucket.container.size();
        }
    }
    else
    {
        boost::ignore_unused(bucket, it);
    }
}

template class BaseStorage<false>;
template class BaseStorage<true>;

}  // namespace bcos::storage