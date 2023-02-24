#pragma once

#include "MemoryStorage.h"
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreeWay4StringView.h>
#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash_fwd.hpp>
#include <boost/functional/hash.hpp>
#include <boost/static_string.hpp>
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>

template <size_t n>
struct boost::hash<boost::container::small_vector<char, n>>
{
    std::size_t operator()(const boost::container::small_vector<char, n>& str) const noexcept
    {
        return boost::hash<std::string_view>{}(std::string_view(str.data(), str.size()));
    }
    std::size_t operator()(std::string_view view) const noexcept
    {
        return boost::hash<std::string_view>{}(view);
    }
};

template <size_t n>
struct std::equal_to<boost::container::small_vector<char, n>>
{
    bool operator()(
        const boost::container::small_vector<char, n>& str, std::string_view view) const noexcept
    {
        return std::string_view(str.data(), str.size()) == view;
    }
    bool operator()(
        std::string_view view, const boost::container::small_vector<char, n>& str) const noexcept
    {
        return std::string_view(str.data(), str.size()) == view;
    }
    bool operator()(const boost::container::small_vector<char, n>& lhs,
        const boost::container::small_vector<char, n>& rhs) const noexcept
    {
        return lhs == rhs;
    }
};

namespace bcos::storage2::string_pool
{
// clang-format off
struct EmptyStringIDError : public bcos::Error {};
// clang-format on

class StringPool
{
public:
    using ID = const void*;

    virtual ~StringPool() = default;
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
    StringID(StringID&&) = default;
    StringID& operator=(const StringID&) = default;
    StringID& operator=(StringID&&) = default;
    ~StringID() = default;

    std::string_view operator*() const
    {
        if (m_pool == nullptr)
        {
            BOOST_THROW_EXCEPTION(EmptyStringIDError{});
        }
        return m_pool->query(m_stringPoolID);
    }

    std::weak_ordering operator<=>(const StringID& rhs) const
    {
        if (m_pool == nullptr)
        {
            if (rhs.m_pool == nullptr)
            {
                return std::weak_ordering::equivalent;
            }
            return std::weak_ordering::less;
        }

        if (rhs.m_pool == nullptr)
        {
            return std::weak_ordering::greater;
        }

        if (std::addressof(m_pool) == std::addressof(rhs.m_pool))
        {
            return m_stringPoolID <=> rhs.m_stringPoolID;
        }

        return m_pool->query(m_stringPoolID) <=> rhs.m_pool->query(rhs.m_stringPoolID);
    }

    bool operator==(const StringID& rhs) const
    {
        if (m_pool == nullptr)
        {
            return rhs.m_pool == nullptr;
        }

        if (std::addressof(m_pool) == std::addressof(rhs.m_pool))
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
    mutable memory_storage::MemoryStorage<StringType, memory_storage::Empty,
        memory_storage::CONCURRENT, boost::hash<StringType>>
        m_storage;

public:
    struct UnexceptStringID : public bcos::Error
    {
    };

    FixedStringPool() noexcept = default;
    FixedStringPool(const FixedStringPool&) = delete;
    FixedStringPool(FixedStringPool&&) = delete;
    FixedStringPool& operator=(const FixedStringPool&) = delete;
    FixedStringPool& operator=(FixedStringPool&&) = delete;
    ~FixedStringPool() noexcept override = default;

    ID add(std::string_view str) override
    {
        // Replace to tbb
        while (true)
        {
            auto itAwaitable = m_storage.read(single(str));
            auto& it = itAwaitable.value();
            std::ignore = it.next();
            auto existsAwaitable = it.hasValue();
            auto exists = existsAwaitable.value();
            if (exists)
            {
                auto keyAwaitable = it.key();
                const auto& key = keyAwaitable.value();
                return std::addressof(key);
            }

            it.release();
            m_storage.write(
                single(StringType(str.begin(), str.end())), single(memory_storage::Empty{}));
        }
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