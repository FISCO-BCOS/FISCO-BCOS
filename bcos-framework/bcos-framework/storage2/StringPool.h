#pragma once

#include "ConcurrentStorage.h"
#include <bcos-utilities/Error.h>
#include <boost/functional/hash.hpp>
#include <boost/static_string.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <string_view>

template <size_t n>
struct boost::hash<boost::static_string<n>>
{
    std::size_t operator()(const boost::static_string<n>& str) const noexcept
    {
        return boost::hash<boost::string_view>{}(str);
    }
    std::size_t operator()(std::string_view view) const noexcept { return boost::hash<std::string_view>{}(view); }
};

template <size_t n>
struct std::equal_to<boost::static_string<n>>
{
    bool operator()(const boost::static_string<n>& str, std::string_view view) const noexcept
    {
        return std::string_view(str.data(), str.size()) == view;
    }
    bool operator()(std::string_view view, const boost::static_string<n>& str) const noexcept
    {
        return std::string_view(str.data(), str.size()) == view;
    }
    bool operator()(const boost::static_string<n>& lhs, const boost::static_string<n>& rhs) const noexcept
    {
        return lhs == rhs;
    }
};

namespace bcos::storage2::string_pool
{
struct OutOfRange : bcos::Error
{
};

template <class StringID>
static std::string_view query(StringID stringID)
{
    return {stringID->data(), stringID->size()};
}

constexpr static size_t DEFAULT_STRING_LENGTH = 62;
template <size_t stringLength = DEFAULT_STRING_LENGTH>
class StringPool
{
private:
    using StringType = boost::static_string<stringLength>;
    concurrent_storage::ConcurrentStorage<StringType> m_storage;

public:
    using StringID = const StringType*;
    StringID add(std::string_view str)
    {
        if (str.size() > stringLength)
        {
            BOOST_THROW_EXCEPTION(OutOfRange{});
        }

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

            it.release();
            m_storage.write(single(StringType(str.begin(), str.end())), single(concurrent_storage::Empty{}));
        }
    }
};

}  // namespace bcos::storage2::string_pool