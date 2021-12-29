#include "StateStorage.h"
#include "../libutilities/Error.h"
#include "bcos-framework/libutilities/BoostLog.h"
#include <oneapi/tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>
#include <tbb/queuing_rw_mutex.h>
#include <tbb/spin_mutex.h>
#include <boost/algorithm/hex.hpp>
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
        for (auto& it : m_data)
        {
            auto& [entryTable, entryKey] = it.first;
            if (entryTable == table)
            {
                if (!_condition || _condition->isValid(entryKey))
                {
                    localKeys.emplace(entryKey, it.second.status());
                }
            }
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
    decltype(m_data)::const_accessor entryIt;
    if (m_data.find(entryIt, std::make_tuple(tableView, keyView)))
    {
        auto& entry = entryIt->second;

        if (entry.status() != Entry::NORMAL)
        {
            entryIt.release();

            STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "DELETED");
            _callback(nullptr, std::nullopt);
        }
        else
        {
            auto optionalEntry = std::make_optional(entry);
            entryIt.release();

            STORAGE_REPORT_GET(tableView, keyView, optionalEntry, "FOUND");
            _callback(nullptr, std::move(optionalEntry));
        }
        return;
    }
    else
    {
        STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "NO ENTRY");
    }

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

            long existsCount = 0;

            size_t i = 0;
            for (auto& key : _keys)
            {
                decltype(m_data)::const_accessor entryIt;
                std::string_view keyView(key);
                if (m_data.find(entryIt, EntryKey(tableView, keyView)))
                {
                    auto& entry = entryIt->second;
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
                    std::get<1>(missinges).emplace_back(std::string(key), i);
                    std::get<0>(missinges).emplace_back(key);
                }

                ++i;
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

    decltype(m_data)::accessor entryIt;
    if (m_data.find(entryIt, EntryKey(tableNameView, keyView)))
    {
        auto& existsEntry = entryIt->second;
        entryOld.emplace(std::move(existsEntry));

        updatedCapacity -= entryOld->size();

        if (entry.status() == Entry::PURGED)
        {
            m_data.erase(entryIt);
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "PURGED");
        }
        else
        {
            STORAGE_REPORT_SET(tableNameView, keyView, entry, "UPDATE");
            entryIt->second = std::move(entry);
            entryIt.release();
        }
    }
    else
    {
        if (entry.status() == Entry::PURGED)
        {
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "PURGED NOT EXISTS");

            callback(nullptr);
            return;
        }

        if (m_data.emplace(
                EntryKey(std::string(tableNameView), std::string(keyView)), std::move(entry)))
        {
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "INSERT");
        }
        else
        {
            auto message = (boost::format("Set row failed because row exists: %s | %s") %
                            tableNameView % keyView)
                               .str();
            STORAGE_LOG(WARNING) << message;
            STORAGE_REPORT_SET(tableNameView, keyView, std::nullopt, "FAIL EXISTS");
        }
    }

    if (m_recoder.local())
    {
        m_recoder.local()->log(
            Recoder::Change(std::string(tableNameView), std::string(keyView), std::move(entryOld)));
    }

    m_capacity += updatedCapacity;

    callback(nullptr);
}

void StateStorage::parallelTraverse(bool onlyDirty,
    std::function<bool(
        const std::string_view& table, const std::string_view& key, const Entry& entry)>
        callback) const
{
    oneapi::tbb::parallel_for_each(
        m_data.begin(), m_data.end(), [&](const std::pair<const EntryKey, Entry>& it) {
            auto& entry = it.second;
            if (!onlyDirty || entry.dirty())
            {
                callback(std::get<0>(it.first), std::get<1>(it.first), entry);
            }
        });
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

crypto::HashType StateStorage::hash(const bcos::crypto::Hash::Ptr& hashImpl)
{
    bcos::crypto::HashType totalHash;

#pragma omp parallel
#pragma omp single
    for (auto& it : m_data)
    {
        auto& entry = it.second;
        if (entry.dirty())
        {
#pragma omp task
            {
                auto& key = it.first;
                auto hash = hashImpl->hash(std::get<0>(key));
                hash ^= hashImpl->hash(std::get<1>(key));

                if (entry.status() != Entry::DELETED)
                {
                    auto value = entry.getField(0);
                    if (c_fileLogLevel >= TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Calc hash, dirty entry: " << std::get<0>(it.first) << " | "
                            << toHex(std::get<1>(it.first)) << " | " << toHex(value);
                    }
                    bcos::bytesConstRef ref((const bcos::byte*)value.data(), value.size());
                    hash ^= hashImpl->hash(ref);
                }
                else
                {
                    if (c_fileLogLevel >= TRACE)
                    {
                        STORAGE_LOG(TRACE) << "Calc hash, deleted entry: " << std::get<0>(it.first)
                                           << " | " << toHex(std::get<1>(it.first));
                    }
                    hash ^= bcos::crypto::HashType(0x1);
                }

#pragma omp critical
                totalHash ^= hash;
            }
        }
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
        if (change.entry)
        {
            decltype(m_data)::accessor entryIt;

            if (m_data.find(entryIt,
                    std::make_tuple(std::string_view(change.table), std::string_view(change.key))))
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert exists: " << change.table << " | "
                                       << toHex(change.key) << " | " << toHex(change.entry->get());
                }
                updateCapacity = change.entry->size() - entryIt->second.size();
                entryIt->second = std::move(*(change.entry));
            }
            else
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE) << "Revert deleted: " << change.table << " | "
                                       << toHex(change.key) << " | " << toHex(change.entry->get());
                }
                updateCapacity = change.entry->size();
                m_data.emplace(std::make_tuple(std::string(change.table), std::string(change.key)),
                    std::move(*(change.entry)));
            }
        }
        else
        {  // nullopt means the key is not exist in m_cache
            decltype(m_data)::const_accessor entryIt;
            if (m_data.find(entryIt,
                    EntryKey(std::string_view(change.table), std::string_view(change.key))))
            {
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {
                    STORAGE_LOG(TRACE)
                        << "Revert insert: " << change.table << " | " << toHex(change.key);
                }

                updateCapacity = 0 - entryIt->second.size();
                m_data.erase(entryIt);
            }
            else
            {
                auto message =
                    (boost::format("Not found rollback entry: %s:%s") % change.table % change.key)
                        .str();

                BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
            }
        }

        m_capacity += updateCapacity;
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
    decltype(m_data)::const_accessor entryIt;
    if (!m_data.emplace(entryIt, EntryKey(std::string(table), std::string(key)), std::move(entry)))
    {
        STORAGE_REPORT_SET(
            std::get<0>(entryIt->first), key, entryIt->second, "IMPORT EXISTS FAILED");

        STORAGE_LOG(WARNING) << "Fail import existsing entry, " << table << " | " << toHex(key);
    }
    else
    {
        STORAGE_REPORT_SET(
            std::get<0>(entryIt->first), key, std::make_optional(entryIt->second), "IMPORT");
        m_capacity += updateCapacity;
    }

    assert(!entryIt.empty());

    return entryIt->second;
}
