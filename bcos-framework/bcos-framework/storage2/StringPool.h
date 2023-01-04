#pragma once

#include "ConcurrentStorage.h"
#include <bcos-utilities/Error.h>
#include <boost/functional/hash.hpp>
#include <functional>
#include <memory>
#include <string_view>

namespace bcos::storage2::string_pool
{

struct OutOfRange : bcos::Error
{
};

struct Item
{
    std::string_view view() const noexcept { return {str.data(), size}; }
    bool operator==(std::string_view lhs) const noexcept { return lhs == view(); }
    bool operator==(const Item& rhs) const noexcept { return view() == rhs.view(); }
    operator std::string_view() const noexcept { return view(); }

    std::array<char, 63> str;
    uint8_t size;
};
using StringID = const Item*;
}  // namespace bcos::storage2::string_pool

template <>
struct boost::hash<bcos::storage2::string_pool::Item>
{
    std::size_t operator()(const bcos::storage2::string_pool::Item& item) const noexcept
    {
        return boost::hash<std::string_view>{}(item.view());
    }
    std::size_t operator()(std::string_view view) const noexcept
    {
        return boost::hash<std::string_view>{}(view);
    }
};

template <>
struct std::equal_to<bcos::storage2::string_pool::Item>
{
    bool operator()(
        const bcos::storage2::string_pool::Item& item, std::string_view view) const noexcept
    {
        return item.view() == view;
    }
    bool operator()(
        std::string_view view, const bcos::storage2::string_pool::Item& item) const noexcept
    {
        return item.view() == view;
    }
    bool operator()(const bcos::storage2::string_pool::Item& lhs,
        const bcos::storage2::string_pool::Item& rhs) const noexcept
    {
        return lhs.view() == rhs.view();
    }
};

namespace bcos::storage2::string_pool
{
class StringPool
{
private:
    concurrent_storage::ConcurrentStorage<Item> m_storage;

public:
    StringID add(std::string_view str)
    {
        while (true)
        {
            auto itAwaitable = m_storage.read(single(str));
            auto& it = itAwaitable.value();
            it.next();

            auto existsAwaitable = it.hasValue();
            auto exists = existsAwaitable.value();
            if (exists)
            {
                auto keyAwaitable = it.key();
                const auto& key = keyAwaitable.value();
                return std::addressof(key);
            }

            // Write item into storage
            it.release();
            Item newItem;
            newItem.size = str.size();
            std::uninitialized_copy_n(str.begin(), str.size(), newItem.str.begin());
            m_storage.write(single(newItem), single(concurrent_storage::Empty{}));
        }
    }

    static std::string_view query(StringID stringID) { return stringID->view(); }
};

}  // namespace bcos::storage2::string_pool