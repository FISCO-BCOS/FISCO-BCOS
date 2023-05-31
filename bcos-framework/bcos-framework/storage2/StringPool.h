#pragma once

#include "MemoryStorage.h"
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreeWay4StringView.h>
#include <oneapi/tbb/concurrent_unordered_set.h>
#include <boost/beast/core/static_string.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/functional/hash.hpp>
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>

namespace bcos::storage2::string_pool
{
// clang-format off
struct EmptyStringIDError : public bcos::Error {};
// clang-format on

class StringPool
{
public:
    using ID = const void*;

    virtual ~StringPool() noexcept = default;
    virtual ID add(std::string_view str) = 0;
    virtual std::string_view query(ID id) const = 0;
};

class StringID
{
    friend class std::hash<StringID>;

private:
    const StringPool* m_pool;
    StringPool::ID m_stringPoolID;

public:
    StringID() : m_pool(nullptr), m_stringPoolID(nullptr) {}
    StringID(const StringPool& pool, StringPool::ID stringPoolID)
      : m_pool(std::addressof(pool)), m_stringPoolID(stringPoolID)
    {}
    StringID(const StringID&) = default;
    StringID(StringID&&) noexcept = default;
    StringID& operator=(const StringID&) = default;
    StringID& operator=(StringID&&) noexcept = default;
    ~StringID() noexcept = default;

    std::string_view operator*() const
    {
        if (m_pool == nullptr)
        {
            BOOST_THROW_EXCEPTION(EmptyStringIDError{});
        }
        return m_pool->query(m_stringPoolID);
    }

    auto operator<=>(const StringID& rhs) const
    {
        if (m_pool == nullptr || rhs.m_pool == nullptr)
        {
            return m_pool <=> rhs.m_pool;
        }
        if (m_pool == rhs.m_pool)
        {
            return m_stringPoolID <=> rhs.m_stringPoolID;
        }

        return m_pool->query(m_stringPoolID) <=> rhs.m_pool->query(rhs.m_stringPoolID);
    }

    bool operator==(const StringID& rhs) const
    {
        if (m_pool == nullptr || rhs.m_pool == nullptr)
        {
            return true;
        }

        if (m_pool == rhs.m_pool)
        {
            return m_stringPoolID == rhs.m_stringPoolID;
        }

        return m_pool->query(m_stringPoolID) == rhs.m_pool->query(rhs.m_stringPoolID);
    }
};

inline StringID makeStringID(StringPool& pool, std::string_view str)
{
    return {pool, pool.add(str)};
}

constexpr static size_t DEFAULT_STRING_LENGTH = 32;
class FixedStringPool : public StringPool
{
private:
    using StringType = boost::container::small_vector<char, DEFAULT_STRING_LENGTH>;
    struct EqualTo
    {
        using is_transparent = std::string_view;

        bool operator()(const StringType& str, std::string_view view) const noexcept
        {
            return std::string_view(str.data(), str.size()) == view;
        }
        bool operator()(std::string_view view, const StringType& str) const noexcept
        {
            return std::string_view(str.data(), str.size()) == view;
        }
        bool operator()(const StringType& lhs, const StringType& rhs) const noexcept
        {
            return lhs == rhs;
        }
    };
    struct Hash
    {
        using transparent_key_equal = EqualTo;

        std::size_t operator()(const StringType& str) const noexcept
        {
            return std::hash<std::string_view>{}(std::string_view(str.data(), str.size()));
        }
        std::size_t operator()(std::string_view view) const noexcept
        {
            return std::hash<std::string_view>{}(view);
        }
    };

    tbb::concurrent_unordered_set<StringType, Hash, EqualTo> m_storage;

public:
    FixedStringPool() noexcept = default;
    FixedStringPool(const FixedStringPool&) = delete;
    FixedStringPool(FixedStringPool&&) = delete;
    FixedStringPool& operator=(const FixedStringPool&) = delete;
    FixedStringPool& operator=(FixedStringPool&&) = delete;
    ~FixedStringPool() noexcept override = default;

    ID add(std::string_view str) override
    {
        auto it = m_storage.find(str);
        if (it == m_storage.end())
        {
            it = m_storage.emplace_hint(it, StringType(str.begin(), str.end()));
        }

        return std::addressof(*it);
    }

    std::string_view query(ID id) const override
    {
        auto* strPtr = (StringType*)id;
        return {strPtr->data(), strPtr->size()};
    }
};

}  // namespace bcos::storage2::string_pool

namespace std
{

inline std::ostream& operator<<(
    std::ostream& stream, bcos::storage2::string_pool::StringID const& stringID)
{
    stream << *stringID;
    return stream;
}

template <>
struct hash<bcos::storage2::string_pool::StringID>
{
    size_t operator()(bcos::storage2::string_pool::StringID const& stringID)
    {
        size_t hash = 0;
        boost::hash_combine(hash, stringID.m_pool);
        boost::hash_combine(hash, stringID.m_stringPoolID);
        return hash;
    }
};
}  // namespace std