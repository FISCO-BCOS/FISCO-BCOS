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
            ;

    StorageWrapper(const StorageWrapper&) = delete;
    StorageWrapper(StorageWrapper&&) = delete;
    StorageWrapper& operator=(const StorageWrapper&) = delete;
    StorageWrapper& operator=(StorageWrapper&&) = delete;

    virtual ~StorageWrapper() = default;

    std::vector<std::string> getPrimaryKeys(
        const std::string_view& table, const std::optional<storage::Condition const>& _condition);

    std::optional<storage::Entry> getRowInternal(
        const std::string_view& table, const std::string_view& _key);

    virtual std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key);

    virtual std::vector<std::optional<storage::Entry>> getRows(const std::string_view& table,
        ::ranges::any_view<std::string_view,
            ::ranges::category::input | ::ranges::category::random_access |
                ::ranges::category::sized>
            keys);

    virtual void setRow(
        const std::string_view& table, const std::string_view& key, storage::Entry entry);

    std::optional<storage::Table> createTable(std::string _tableName, std::string _valueFields);

    std::tuple<Error::UniquePtr, std::optional<storage::Table>> createTableWithoutException(
        std::string _tableName, std::string _valueFields);

    std::optional<storage::Table> openTable(std::string_view tableName);

    std::pair<size_t, Error::Ptr> count(const std::string_view& _table);

    std::tuple<Error::UniquePtr, std::optional<storage::Table>> openTableWithoutException(
        std::string_view tableName);

    void setRecoder(storage::Recoder::Ptr recoder);

    void setCodeCache(EntryCachePtr cache);
    void setCodeHashCache(EntryCachePtr cache);

    storage::StateStorageInterface::Ptr getRawStorage();

private:
    storage::StateStorageInterface::Ptr m_storage;
    bcos::storage::Recoder::Ptr m_recoder;

    EntryCachePtr m_codeCache;
    EntryCachePtr m_codeHashCache;
    tbb::concurrent_unordered_map<std::string, std::optional<storage::Table>> m_tableCache;
};
}  // namespace bcos::storage