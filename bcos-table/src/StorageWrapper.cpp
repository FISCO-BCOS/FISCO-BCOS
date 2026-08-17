/*
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "StorageWrapper.h"

using namespace bcos::storage;

StorageWrapper::StorageWrapper(
    storage::StateStorageInterface::Ptr storage, bcos::storage::Recoder::Ptr recoder)
  : m_storage(std::move(storage)), m_recoder(std::move(recoder))
{}

std::vector<std::string> StorageWrapper::getPrimaryKeys(
    const std::string_view& table, const std::optional<storage::Condition const>& _condition)
{
    GetPrimaryKeysReponse value;
    m_storage->asyncGetPrimaryKeys(table, _condition, [&value](auto&& error, auto&& keys) mutable {
        value = {std::move(error), std::move(keys)};
    });

    setRecoder(m_recoder);

    auto& [error, keys] = value;
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }

    return std::move(keys);
}

std::optional<Entry> StorageWrapper::getRowInternal(
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

std::optional<Entry> StorageWrapper::getRow(
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
    if (m_codeHashCache && _key.compare(M_ACCOUNT_CODE_HASH) == 0)
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

    return getRowInternal(table, _key);
}

std::vector<std::optional<Entry>> StorageWrapper::getRows(const std::string_view& table,
    ::ranges::any_view<std::string_view,
        ::ranges::category::input | ::ranges::category::random_access |
            ::ranges::category::sized>
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

void StorageWrapper::setRow(
    const std::string_view& table, const std::string_view& key, Entry entry)
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

std::optional<Table> StorageWrapper::createTable(
    std::string _tableName, std::string _valueFields)
{
    auto ret = createTableWithoutException(std::move(_tableName), std::move(_valueFields));
    if (std::get<0>(ret))
    {
        BOOST_THROW_EXCEPTION(*(std::get<0>(ret)));
    }

    return std::get<1>(ret);
}

std::tuple<bcos::Error::UniquePtr, std::optional<Table>>
StorageWrapper::createTableWithoutException(std::string _tableName, std::string _valueFields)
{
    std::promise<OpenTableResponse> createPromise;
    m_storage->asyncCreateTable(std::move(_tableName), std::move(_valueFields),
        [&](Error::UniquePtr&& error, auto&& table) mutable {
            createPromise.set_value({std::move(error), std::move(table)});
        });
    auto value = createPromise.get_future().get();
    return value;
}

std::optional<Table> StorageWrapper::openTable(std::string_view tableName)
{
    auto it = m_tableCache.find(std::string(tableName));
    if (it != m_tableCache.end())
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

std::pair<size_t, bcos::Error::Ptr> StorageWrapper::count(const std::string_view& _table)
{
    return m_storage->count(_table);
}

std::tuple<bcos::Error::UniquePtr, std::optional<Table>>
StorageWrapper::openTableWithoutException(std::string_view tableName)
{
    std::promise<OpenTableResponse> openPromise;
    m_storage->asyncOpenTable(tableName, [&](auto&& error, auto&& table) mutable {
        openPromise.set_value({std::move(error), std::move(table)});
    });
    auto value = openPromise.get_future().get();
    return value;
}

void StorageWrapper::setRecoder(storage::Recoder::Ptr recoder)
{
    m_storage->setRecoder(std::move(recoder));
}

void StorageWrapper::setCodeCache(EntryCachePtr cache)
{
    m_codeCache = std::move(cache);
}

void StorageWrapper::setCodeHashCache(EntryCachePtr cache)
{
    m_codeHashCache = std::move(cache);
}

StateStorageInterface::Ptr StorageWrapper::getRawStorage()
{
    return m_storage;
}