#pragma once

#include <ranges>

namespace bcos::storage
{

template <class StorageType>
concept Storage = requires(StorageType storage)
{
    typename StorageType::Table;
    typename StorageType::Key;

    storage.getRow(typename StorageType::TABLE_TYPE{}, typename StorageType::KEY_TYPE{});
    storage.getRows(typename StorageType::TABLE_TYPE{}, std::ranges::range keylist);
};

// class StorageInterface
// {
// public:
//     static constexpr const char SYS_TABLES[] = "s_tables";
//     static constexpr const char SYS_TABLE_VALUE_FIELDS[] = "key_field,value_fields";

//     static TableInfo::ConstPtr getSysTableInfo(std::string_view tableName);

//     using Ptr = std::shared_ptr<StorageInterface>;

//     virtual ~StorageInterface() = default;

//     virtual void asyncGetPrimaryKeys(std::string_view table, const std::optional<Condition const>& _condition,
//         std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) = 0;

//     virtual void asyncGetRow(std::string_view table, std::string_view _key,
//         std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) = 0;

//     virtual void asyncGetRows(std::string_view table,
//         const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>& _keys,
//         std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) = 0;

//     virtual void asyncSetRow(
//         std::string_view table, std::string_view key, Entry entry, std::function<void(Error::UniquePtr)> callback) =
//         0;

//     virtual void asyncCreateTable(std::string _tableName, std::string _valueFields,
//         std::function<void(Error::UniquePtr, std::optional<Table>)> callback);

//     virtual void asyncOpenTable(
//         std::string_view tableName, std::function<void(Error::UniquePtr, std::optional<Table>)> callback);

//     virtual void asyncGetTableInfo(
//         std::string_view tableName, std::function<void(Error::UniquePtr, TableInfo::ConstPtr)> callback);
// };
}  // namespace bcos::storage