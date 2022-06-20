#pragma once

#include <boost/type_traits.hpp>
#include <ranges>
#include <type_traits>

namespace bcos::storage
{

template <class Function, class KeyType>
concept GetRowsFunction = requires(typename boost::function_traits<Function>::arg1_type getRowArg1,
    typename boost::function_traits<Function>::arg2_type getRowsArg2,
    typename std::ranges::range_value_t<typename boost::function_traits<Function>::arg2_type> getRowsArg2ValueType)
{
    {
        getRowArg1
        } -> std::convertible_to<KeyType>;
    {
        getRowsArg2
        } -> std::ranges::range;
    {
        getRowsArg2ValueType
        } -> std::same_as<KeyType>;
};

template <class StorageType>
concept Storage = requires(StorageType storage, decltype(&StorageType::getRows) getRowsFunction)
{
    typename StorageType::TableType;
    typename StorageType::KeyType;

    storage.getRow(typename StorageType::TableType{}, typename StorageType::KeyType{});
    {
        getRowsFunction
        } -> GetRowsFunction<typename StorageType::KeyType>;
};

class StorageImpl
{
public:
    using TableType = std::string_view;
    using KeyType = std::string_view;

    void getRow([[maybe_unused]] TableType table, [[maybe_unused]] KeyType key) {}
    void getRows([[maybe_unused]] TableType table, [[maybe_unused]] std::vector<KeyType> keys) {}
};

static_assert(GetRowsFunction<decltype(&StorageImpl::getRows), std::string_view>, "Not a storage!");
static_assert(Storage<StorageImpl>, "fail!");

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