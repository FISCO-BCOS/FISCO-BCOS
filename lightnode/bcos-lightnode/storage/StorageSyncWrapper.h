#pragma once

#include <bcos-concepts/Basic.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-framework/storage/Entry.h>
#include <boost/throw_exception.hpp>
#include <type_traits>

namespace bcos::storage
{

template <class StorageType>
class StorageSyncWrapper
  : public bcos::concepts::storage::StorageBase<StorageSyncWrapper<StorageType>>
{
public:
    StorageSyncWrapper(StorageType storage) : m_storage(std::move(storage)) {}
    StorageSyncWrapper(const StorageSyncWrapper&) = default;
    StorageSyncWrapper(StorageSyncWrapper&&) = default;
    StorageSyncWrapper& operator=(const StorageSyncWrapper&) = default;
    StorageSyncWrapper& operator=(StorageSyncWrapper&&) = default;
    ~StorageSyncWrapper() = default;

    std::optional<Entry> impl_getRow(std::string_view table, std::string_view key)
    {
        Error::UniquePtr error;
        std::optional<storage::Entry> entry;

        storage().asyncGetRow(
            table, key, [&error, &entry](Error::UniquePtr errorOut, std::optional<Entry> entryOut) {
                error = std::move(errorOut);
                entry = std::move(entryOut);
            });

        if (error)
            BOOST_THROW_EXCEPTION(*error);

        return entry;
    }

    std::vector<std::optional<Entry>> impl_getRows(
        std::string_view table, RANGES::range auto const& keys)
    {
        Error::UniquePtr error;
        std::vector<std::optional<Entry>> entries;

        using ValueType = RANGES::range_value_t<std::remove_cvref_t<decltype(keys)>>;
        auto callback = [&error, &entries](Error::UniquePtr errorOut,
                            std::vector<std::optional<Entry>> entriesOut) {
            error = std::move(errorOut);
            entries = std::move(entriesOut);
        };

        if constexpr (std::is_same_v<ValueType, std::string_view>)
        {
            gsl::span<std::string_view const> view(RANGES::data(keys), RANGES::size(keys));
            storage().asyncGetRows(table, view, std::move(callback));
        }
        else if constexpr (std::is_same_v<ValueType, std::string>)
        {
            gsl::span<std::string const> view(RANGES::data(keys), RANGES::size(keys));
            storage().asyncGetRows(table, view, std::move(callback));
        }
        else if constexpr (bcos::concepts::ByteBuffer<ValueType>)
        {
            std::vector<std::string_view> viewArray;
            viewArray.reserve(RANGES::size(keys));
            for (auto& it : keys)
            {
                viewArray.emplace_back(
                    std::string_view((const char*)RANGES::data(it), RANGES::size(it)));
            }

            storage().asyncGetRows(table, viewArray, std::move(callback));
        }
        else
        {
            static_assert(!sizeof(keys), "Unknown key type!");
        }

        if (error)
            BOOST_THROW_EXCEPTION(*error);

        return entries;
    }

    void impl_setRow(std::string_view table, std::string_view key, Entry entry)
    {
        Error::UniquePtr error;

        storage().asyncSetRow(table, key, std::move(entry),
            [&error](Error::UniquePtr errorOut) { error = std::move(errorOut); });

        if (error)
            BOOST_THROW_EXCEPTION(*error);
    }

    void impl_createTable(std::string tableName)
    {
        Error::UniquePtr error;

        storage().asyncCreateTable(std::move(tableName), std::string{},
            [&error](Error::UniquePtr errorOut, [[maybe_unused]] auto&& table) {
                error = std::move(errorOut);
            });
        if (error)
            BOOST_THROW_EXCEPTION(*error);
    };

private:
    constexpr auto& storage() { return bcos::concepts::getRef(m_storage); }

    StorageType m_storage;
};

static_assert(bcos::concepts::storage::Storage<StorageSyncWrapper<int>>, "fail!");
}  // namespace bcos::storage