#pragma once

#include "../Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <boost/iterator/iterator_categories.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <thread>
#include <vector>

namespace bcos::executor
{
using GetPrimaryKeysReponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;
using GetRowResponse = std::tuple<Error::UniquePtr, std::optional<storage::Entry>>;
using GetRowsResponse = std::tuple<Error::UniquePtr, std::vector<std::optional<storage::Entry>>>;
using SetRowResponse = std::tuple<Error::UniquePtr>;
using OpenTableResponse = std::tuple<Error::UniquePtr, std::optional<storage::Table>>;


class StorageWrapper
{
public:
    StorageWrapper(storage::StateStorageInterface::Ptr storage, bcos::storage::Recoder::Ptr recoder)
      : m_storage(std::move(storage)), m_recoder(recoder)
    {}

    StorageWrapper(const StorageWrapper&) = delete;
    StorageWrapper(StorageWrapper&&) = delete;
    StorageWrapper& operator=(const StorageWrapper&) = delete;
    StorageWrapper& operator=(StorageWrapper&&) = delete;

    virtual ~StorageWrapper() {}

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

    virtual std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key)
    {
        GetRowResponse value;
        m_storage->asyncGetRow(table, _key, [&value](auto&& error, auto&& entry) mutable {
            value = {std::move(error), std::move(entry)};
        });

        auto& [error, entry] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(entry);
    }

    virtual std::vector<std::optional<storage::Entry>> getRows(
        const std::string_view& table, const std::variant<const gsl::span<std::string_view const>,
                                           const gsl::span<std::string const>>& _keys)
    {
        GetRowsResponse value;
        m_storage->asyncGetRows(table, _keys, [&value](auto&& error, auto&& entries) mutable {
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
        auto ret = openTableWithoutException(tableName);
        if (std::get<0>(ret))
        {
            BOOST_THROW_EXCEPTION(*(std::get<0>(ret)));
        }

        return std::get<1>(ret);
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

private:
    storage::StateStorageInterface::Ptr m_storage;
    bcos::storage::Recoder::Ptr m_recoder;
};
}  // namespace bcos::executor