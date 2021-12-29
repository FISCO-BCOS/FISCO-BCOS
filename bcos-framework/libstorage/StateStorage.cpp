#include "StateStorage.h"
#include "../libutilities/Error.h"
#include "bcos-framework/libutilities/BoostLog.h"
#include <oneapi/tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>
#include <tbb/queuing_rw_mutex.h>
#include <tbb/spin_mutex.h>
#include <boost/algorithm/hex.hpp>
#include <boost/container_hash/hash_fwd.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/crc.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cctype>
#include <iomanip>
#include <ios>
#include <iterator>
#include <optional>
#include <thread>

using namespace bcos;
using namespace bcos::storage;

void StateStorage::asyncGetPrimaryKeys(std::string_view table,
    const std::optional<Condition const>& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback)
{
    std::map<std::string_view, storage::Entry::Status> localKeys;

    if (m_enableTraverse)
    {
#pragma omp parallel for
        for (size_t i = 0; i < m_buckets.size(); ++i)
        {
            auto& bucket = m_buckets[i];
            std::unique_lock<std::mutex> lock(bucket.mutex);

            decltype(localKeys) bucketKeys;
            for (auto& it : bucket.container)
            {
                if (it.table == table && (!_condition || _condition->isValid(it.key)))
                {
                    bucketKeys.emplace(it.key, it.entry.status());
                }
            }

#pragma omp critical
            localKeys.merge(std::move(bucketKeys));
        }
    }

    auto prev = getPrev();
    if (!prev)
    {
        std::vector<std::string> resultKeys;
        for (auto& localIt : localKeys)
        {
            if (localIt.second == Entry::NORMAL)
            {
                resultKeys.push_back(std::string(localIt.first));
            }
        }

        _callback(nullptr, std::move(resultKeys));
        return;
    }

    prev->asyncGetPrimaryKeys(table, _condition,
        [localKeys = std::move(localKeys), callback = std::move(_callback)](
            auto&& error, auto&& remoteKeys) mutable {
            if (error)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             StorageError::ReadError, "Get primary keys from prev failed!", *error),
                    std::vector<std::string>());
                return;
            }

            for (auto it = remoteKeys.begin(); it != remoteKeys.end();)
            {
                bool deleted = false;

                auto localIt = localKeys.find(*it);
                if (localIt != localKeys.end())
                {
                    if (localIt->second != Entry::NORMAL)
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
                if (localIt.second == Entry::NORMAL)
                {
                    remoteKeys.push_back(std::string(localIt.first));
                }
            }

            callback(nullptr, std::move(remoteKeys));
        });
}

void StateStorage::asyncGetRow(std::string_view tableView, std::string_view keyView,
    std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback)
{
    auto [bucket, lock] = getBucket(tableView, keyView);

    auto it = bucket->container.get<0>().find(std::make_tuple(tableView, keyView));
    if (it != bucket->container.get<0>().end())
    {
        auto& entry = it->entry;

        if (entry.status() != Entry::NORMAL)
        {
            lock.unlock();

            STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "DELETED");
            _callback(nullptr, std::nullopt);
        }
        else
        {
            auto optionalEntry = std::make_optional(entry);
            lock.unlock();

            STORAGE_REPORT_GET(tableView, keyView, optionalEntry, "FOUND");
            _callback(nullptr, std::move(optionalEntry));
        }
        return;
    }
    else
    {
        STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "NO ENTRY");
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
                    _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                  StorageError::ReadError, "Get row from storage failed!", *error),
                        {});
                    return;
                }

                if (entry)
                {
                    STORAGE_REPORT_GET(table, key, entry, "PREV FOUND");

                    _callback(nullptr,
                        std::make_optional(importExistingEntry(table, key, std::move(*entry))));
                }
                else
                {
                    STORAGE_REPORT_GET(table, key, std::nullopt, "PREV NOT FOUND");

                    _callback(nullptr, std::nullopt);
                }
            });
    }
    else
    {
        _callback(nullptr, std::nullopt);
    }
}

void StateStorage::asyncGetRows(std::string_view tableView,
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
{
    std::visit(
        [this, &tableView, &_callback](auto&& _keys) {
            std::vector<std::optional<Entry>> results(_keys.size());
            auto missinges = std::tuple<std::vector<std::string_view>,
                std::vector<std::tuple<std::string, size_t>>>();

            std::atomic_long existsCount = 0;

#pragma omp parallel for
            for (gsl::index i = 0; i < _keys.size(); ++i)
            {
                auto [bucket, lock] = getBucket(tableView, _keys[i]);

                auto it =
                    bucket->container.find(std::make_tuple(tableView, std::string_view(_keys[i])));
                if (it != bucket->container.end())
                {
                    auto& entry = it->entry;
                    if (entry.status() == Entry::NORMAL)
                    {
                        results[i].emplace(entry);
                    }
                    else
                    {
                        results[i] = std::nullopt;
                    }
                    ++existsCount;
                }
                else
                {
#pragma omp critical
                    {
                        std::get<1>(missinges).emplace_back(std::string(_keys[i]), i);
                        std::get<0>(missinges).emplace_back(_keys[i]);
                    }
                }
            }

            auto prev = getPrev();
            if (existsCount < _keys.size() && prev)
            {
                prev->asyncGetRows(tableView, std::get<0>(missinges),
                    [this, table = std::string(tableView), callback = std::move(_callback),
                        missingIndexes = std::move(std::get<1>(missinges)),
                        results = std::move(results)](
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
                                results[std::get<1>(missingIndexes[i])].emplace(importExistingEntry(
                                    table, std::move(std::get<0>(missingIndexes[i])),
                                    std::move(*entry)));
                            }
                        }

                        callback(nullptr, std::move(results));
                    });
            }
            else
            {
                _callback(nullptr, std::move(results));
            }
        },
        _keys);
}

void StateStorage::asyncSetRow(std::string_view tableNameView, std::string_view keyView,
    Entry entry, std::function<void(Error::UniquePtr)> callback)
{
    if (m_readOnly)
    {
        callback(
            BCOS_ERROR_UNIQUE_PTR(StorageError::ReadOnly, "Try to operate a read-only storage"));
        return;
    }

    ssize_t updatedCapacity = entry.size();
    std::optional<Entry> entryOld;

    auto [bucket, lock] = getBucket(tableNameView, keyView);

    auto it = bucket->container.find(std::make_tuple(tableNameView, keyView));
    if (it != bucket->container.end())
    {
        auto& existsEntry = it->entry;
        entryOld.emplace(std::move(existsEntry));

        updatedCapacity -= entryOld->size();

        if (entry.status() == Entry::PURGED)
        {
            bucket->container.erase(it);
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "PURGED");
        }
        else
        {
            STORAGE_REPORT_SET(tableNameView, keyView, entry, "UPDATE");
            bucket->container.modify(
                it, [&entry](Data& data) { data.entry = std::move(entry); });
        }
    }
    else
    {
        if (entry.status() == Entry::PURGED)
        {
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "PURGED NOT EXISTS");

            lock.unlock();
            callback(nullptr);
            return;
        }

        bucket->container.emplace(
            Data{std::string(tableNameView), std::string(keyView), std::move(entry)});

        STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "INSERT");
    }

    if (m_recoder.local())
    {
        m_recoder.local()->log(
            Recoder::Change(std::string(tableNameView), std::string(keyView), std::move(entryOld)));
    }

    bucket->capacity += updatedCapacity;

    lock.unlock();
    callback(nullptr);
}

void StateStorage::parallelTraverse(bool onlyDirty,
    std::function<bool(
        const std::string_view& table, const std::string_view& key, const Entry& entry)>
        callback) const
{
#pragma omp parallel for
    for (size_t i = 0; i < m_buckets.size(); ++i)
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
}

std::optional<Table> StateStorage::openTable(const std::string_view& tableName)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> openPromise;
    asyncOpenTable(tableName, [&](auto&& error, auto&& table) {
        openPromise.set_value({std::move(error), std::move(table)});
    });

    auto [error, table] = openPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }

    return table;
}

std::optional<Table> StateStorage::createTable(std::string _tableName, std::string _valueFields)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    asyncCreateTable(
        _tableName, _valueFields, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
            createPromise.set_value({std::move(error), std::move(table)});
        });
    auto [error, table] = createPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }

    return table;
}

crypto::HashType StateStorage::hash(const bcos::crypto::Hash::Ptr& hashImpl) const
{
    bcos::crypto::HashType totalHash;

#pragma omp parallel for
    for (size_t i = 0; i < m_buckets.size(); ++i)
    {
        auto& bucket = m_buckets[i];
        bcos::crypto::HashType bucketHash;

        for (auto& it : bucket.container)
        {
            auto& entry = it.entry;
            if (entry.dirty())
            {
                auto hash = hashImpl->hash(it.table);
                hash ^= hashImpl->hash(it.key);

                if (entry.status() != Entry::DELETED)
                {
                    auto value = entry.getField(0);
                    if (c_fileLogLevel >= TRACE)
                    {
                        STORAGE_LOG(TRACE) << "Calc hash, dirty entry: " << it.table << " | "
                                           << toHex(it.key) << " | " << toHex(value);
                    }
                    bcos::bytesConstRef ref((const bcos::byte*)value.data(), value.size());
                    hash ^= hashImpl->hash(ref);
                }
                else
                {
                    if (c_fileLogLevel >= TRACE)
                    {
                        STORAGE_LOG(TRACE) << "Calc hash, deleted entry: " << it.table << " | "
                                           << toHex(toHex(it.key));
                    }
                    hash ^= bcos::crypto::HashType(0x1);
                }
                bucketHash ^= hash;
            }
        }
#pragma omp critical
        totalHash ^= bucketHash;
    }

    return totalHash;
}

void StateStorage::rollback(const Recoder& recoder)
{
    if (m_readOnly)
    {
        return;
    }

    for (auto& change : recoder)
    {
        ssize_t updateCapacity = 0;
        auto [bucket, lock] = getBucket(change.table, change.key);
        auto it = bucket->container.find(
            std::make_tuple(std::string_view(change.table), std::string_view(change.key)));
        if (change.entry)
        {
            if (it != bucket->container.end())
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert exists: " << change.table << " | "
                                       << toHex(change.key) << " | " << toHex(change.entry->get());
                }

                updateCapacity = change.entry->size() - it->entry.size();

                auto& rollbackEntry = change.entry;
                bucket->container.modify(it,
                    [&rollbackEntry](Data& data) { data.entry = std::move(*rollbackEntry); });
            }
            else
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert deleted: " << change.table << " | "
                                       << toHex(change.key) << " | " << toHex(change.entry->get());
                }
                updateCapacity = change.entry->size();
                bucket->container.emplace(
                    Data{change.table, change.key, std::move(*(change.entry))});
            }
        }
        else
        {  // nullopt means the key is not exist in m_cache
            if (it != bucket->container.end())
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
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
                    (boost::format("Not found rollback entry: %s:%s") % change.table % change.key)
                        .str();

                BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
            }
        }

        bucket->capacity += updateCapacity;
    }
}

Entry StateStorage::importExistingEntry(std::string_view table, std::string_view key, Entry entry)
{
    if (m_readOnly)
    {
        return entry;
    }

    entry.setDirty(false);

    auto updateCapacity = entry.size();

    auto [bucket, lock] = getBucket(table, key);
    auto it = bucket->container.find(std::make_tuple(table, key));

    if (it == bucket->container.get<0>().end())
    {
        STORAGE_REPORT_SET(
            std::get<0>(entryIt->first), key, std::make_optional(entryIt->second), "IMPORT");
        it = bucket->container
                 .emplace(Data{std::string(table), std::string(key), std::move(entry)})
                 .first;

        bucket->capacity += updateCapacity;
    }
    else
    {
        STORAGE_REPORT_SET(
            std::get<0>(entryIt->first), key, entryIt->second, "IMPORT EXISTS FAILED");

        STORAGE_LOG(WARNING) << "Fail import existsing entry, " << table << " | " << toHex(key);
    }

    return it->entry;
}

std::tuple<StateStorage::Bucket*, std::unique_lock<std::mutex>> StateStorage::getBucket(
    std::string_view table, std::string_view key)
{
    auto hash = std::hash<std::string_view>{}(table);
    boost::hash_combine(hash, std::hash<std::string_view>{}(key));
    auto index = hash % m_buckets.size();

    auto& bucket = m_buckets[index];
    return std::make_tuple(&bucket, std::unique_lock<std::mutex>(bucket.mutex));
}