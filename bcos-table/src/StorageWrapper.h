#pragma once

#include "bcos-framework/storage/EntryCache.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace bcos::storage
{
using GetPrimaryKeysReponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;
using GetRowResponse = std::tuple<Error::UniquePtr, std::optional<storage::Entry>>;
using GetRowsResponse = std::tuple<Error::UniquePtr, std::vector<std::optional<storage::Entry>>>;
using SetRowResponse = std::tuple<Error::UniquePtr>;
using OpenTableResponse = std::tuple<Error::UniquePtr, std::optional<storage::Table>>;

constexpr static std::string_view M_SYS_CODE_BINARY{"s_code_binary"};
constexpr static std::string_view M_ACCOUNT_CODE_HASH{"codeHash"};

class StorageWrapper
{
public:
    StorageWrapper(storage::StateStorageInterface::Ptr storage, bcos::storage::Recoder::Ptr recoder)
      : m_storage(std::move(storage)), m_recoder(std::move(recoder))
    {}

    StorageWrapper(const StorageWrapper&) = delete;
    StorageWrapper(StorageWrapper&&) = delete;
    StorageWrapper& operator=(const StorageWrapper&) = delete;
    StorageWrapper& operator=(StorageWrapper&&) = delete;

    virtual ~StorageWrapper() = default;

    std::vector<std::string> getPrimaryKeys(
        const std::string_view& table, const std::optional<storage::Condition const>& _condition)
    {
        GetPrimaryKeysReponse value;
        m_storage->asyncGetPrimaryKeys(
            table, _condition, [&value](auto&& error, auto&& keys) mutable {
                value = {std::move(error), std::move(keys)};
            });

        // After coroutine switch, set the recoder
        setRecoder(m_recoder);

        auto& [error, keys] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(keys);
    }

    std::optional<storage::Entry> getRowInternal(
        const std::string_view& table, const std::string_view& _key)
    {
        GetRowResponse value;
        m_storage->asyncGetRow(table, _key, [&value](auto&& error, auto&& entry) mutable {
            value = {std::forward<decltype(error)>(error), std::forward<decltype(entry)>(entry)};
        });

        auto& [error, entry] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(entry);
    }

    virtual std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key)
    {
        if (m_codeCache && table.compare(M_SYS_CODE_BINARY) == 0)
        {
            auto it = m_codeCache->find(std::string(_key));
            if (it != m_codeCache->end())
            {
                return it->second;
            }

            auto code = getRowInternal(table, _key);
            if (code.has_value())
            {
                m_codeCache->emplace(std::string(_key), code);
            }

            return code;
        }
        else if (m_codeHashCache && _key.compare(M_ACCOUNT_CODE_HASH) == 0)
        {
            auto it = m_codeHashCache->find(std::string(table));
            if (it != m_codeHashCache->end())
            {
                return it->second;
            }

            auto codeHash = getRowInternal(table, _key);
            if (codeHash.has_value())
            {
                m_codeHashCache->emplace(std::string(table), codeHash);
            }

            return codeHash;
        }
        else
        {
            return getRowInternal(table, _key);
        }
    }

    virtual std::vector<std::optional<storage::Entry>> getRows(const std::string_view& table,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys)
    {
        GetRowsResponse value;
        m_storage->asyncGetRows(table, keys, [&value](auto&& error, auto&& entries) mutable {
            value = {std::move(error), std::move(entries)};
        });


        auto& [error, entries] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(entries);
    }

    virtual void setRow(
        const std::string_view& table, const std::string_view& key, storage::Entry entry)
    {
        SetRowResponse value;

        m_storage->asyncSetRow(table, key, std::move(entry),
            [&value](auto&& error) mutable { value = std::tuple{std::move(error)}; });

        auto& [error] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
    }

    std::optional<storage::Table> createTable(std::string _tableName, std::string _valueFields)
    {
        auto ret = createTableWithoutException(_tableName, _valueFields);
        if (std::get<0>(ret))
        {
            BOOST_THROW_EXCEPTION(*(std::get<0>(ret)));
        }

        return std::get<1>(ret);
    }

    std::tuple<Error::UniquePtr, std::optional<storage::Table>> createTableWithoutException(
        std::string _tableName, std::string _valueFields)
    {
        std::promise<OpenTableResponse> createPromise;
        m_storage->asyncCreateTable(std::move(_tableName), std::move(_valueFields),
            [&](Error::UniquePtr&& error, auto&& table) mutable {
                createPromise.set_value({std::move(error), std::move(table)});
            });
        auto value = createPromise.get_future().get();
        return value;
    }

    std::optional<storage::Table> openTable(std::string_view tableName)
    {
        auto it = m_tableCache.find(std::string(tableName));
        if ( it != m_tableCache.end())
        {
            return it->second;
        }

        auto ret = openTableWithoutException(tableName);
        if (std::get<0>(ret))
        {
            BOOST_THROW_EXCEPTION(*(std::get<0>(ret)));
        }
        auto table = std::get<1>(ret);
        if (table)
        {
            m_tableCache.insert(std::make_pair(std::string(tableName), std::get<1>(ret)));
        }
        return table;
    }

    std::pair<size_t, Error::Ptr> count(const std::string_view& _table)
    {
        return m_storage->count(_table);
    }

    std::tuple<Error::UniquePtr, std::optional<storage::Table>> openTableWithoutException(
        std::string_view tableName)
    {
        std::promise<OpenTableResponse> openPromise;
        m_storage->asyncOpenTable(tableName, [&](auto&& error, auto&& table) mutable {
            openPromise.set_value({std::move(error), std::move(table)});
        });
        auto value = openPromise.get_future().get();
        return value;
    }

    void setRecoder(storage::Recoder::Ptr recoder) { m_storage->setRecoder(std::move(recoder)); }

    void setCodeCache(EntryCachePtr cache) { m_codeCache = cache; }
    void setCodeHashCache(EntryCachePtr cache) { m_codeHashCache = cache; }

private:
    storage::StateStorageInterface::Ptr m_storage;
    bcos::storage::Recoder::Ptr m_recoder;

    EntryCachePtr m_codeCache;
    EntryCachePtr m_codeHashCache;
    tbb::concurrent_unordered_map<std::string, std::optional<storage::Table>> m_tableCache;
};
}  // namespace bcos::storage