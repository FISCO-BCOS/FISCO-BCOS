#pragma once
#include "../storage/Entry.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/Overloaded.h"
#include "bcos-utilities/Ranges.h"
#include "bcos-utilities/ThreeWay4StringView.h"
#include "evmc/evmc.h"
#include <boost/algorithm/hex.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>
#include <variant>

namespace bcos::transaction_executor
{
struct NotStringKeyError : public bcos::Exception
{
};
struct UnableToCompareError : public bcos::Exception
{
};

using StateValue = storage::Entry;

class StateKey
{
private:
    friend class StateKeyView;
    struct EVMKey
    {
        evmc_address address;
        evmc_bytes32 key;
    };
    struct StringKey
    {
        std::string tableKey;
        size_t split{};
    };
    std::variant<EVMKey, StringKey> m_key;

public:
    StateKey(const evmc_address& address, const evmc_bytes32& key) : m_key(EVMKey{address, key}) {}
    StateKey(std::string_view table, std::string_view key)
    {
        std::string tableKey;
        tableKey.reserve(table.size() + 1 + key.size());
        tableKey.insert(tableKey.end(), table.begin(), table.end());
        auto split = tableKey.size();
        tableKey.push_back(':');
        tableKey.insert(tableKey.end(), key.begin(), key.end());
        m_key = StringKey{std::move(tableKey), split};
    }
    StateKey(const StateKey&) = default;
    StateKey(StateKey&&) noexcept = default;
    StateKey& operator=(const StateKey&) = default;
    StateKey& operator=(StateKey&&) noexcept = default;
    ~StateKey() noexcept = default;

    // 将存储的值修改为可以直接作为rocksdb key的格式
    bool convertToStringKey() &
    {
        static constexpr std::string_view USER_APPS_PREFIX = "/apps/";

        if (std::holds_alternative<StringKey>(m_key))
        {
            return false;
        }
        auto& evmKey = std::get<EVMKey>(m_key);

        std::string tableKey;
        tableKey.reserve(USER_APPS_PREFIX.size() + sizeof(evmKey.address) * 2 + sizeof(evmKey.key));
        tableKey.append(USER_APPS_PREFIX);
        boost::algorithm::hex_lower((const char*)evmKey.address.bytes,
            (const char*)evmKey.address.bytes + sizeof(evmKey.address.bytes),
            std::back_inserter(tableKey));
        size_t split = tableKey.size();
        tableKey.push_back(':');
        tableKey.append(std::string_view((const char*)evmKey.key.bytes, sizeof(evmKey.key.bytes)));
        m_key = StringKey{std::move(tableKey), split};
        return true;
    }

    std::string_view getStringKey() const&
    {
        if (!std::holds_alternative<StringKey>(m_key))
        {
            BOOST_THROW_EXCEPTION(NotStringKeyError{});
        }

        return std::string_view(std::get<StringKey>(m_key).tableKey);
    }

    friend std::strong_ordering operator<=>(const StateKey& lhs, const StateKey& rhs)
    {
        return std::visit(overloaded{[](const EVMKey& lhsKey, const EVMKey& rhsKey) {
                                         auto lhsStringView = std::string_view(
                                             (const char*)std::addressof(lhsKey), sizeof(lhsKey));
                                         auto rhsStringView = std::string_view(
                                             (const char*)std::addressof(rhsKey), sizeof(rhsKey));
                                         return lhsStringView <=> rhsStringView;
                                     },
                              [](const StringKey& lhsKey, const StringKey& rhsKey) {
                                  return lhsKey.tableKey <=> rhsKey.tableKey;
                              },
                              [](auto const& lhsKey, auto const& rhsKey) {
                                  BOOST_THROW_EXCEPTION(UnableToCompareError{});
                                  return std::strong_ordering::equivalent;
                              }},
            lhs.m_key, rhs.m_key);
    }
    friend bool operator==(const StateKey& lhs, const StateKey& rhs)
    {
        return std::is_eq(lhs <=> rhs);
    }
};

class StateKeyView
{
private:
    struct EVMKeyView
    {
        evmc_address const* address;
        evmc_bytes32 const* key;
    };

    struct StringKeyView
    {
        std::string_view addressKey;
        size_t split{};
    };

    // Key一般不会超过INT32_MAX, 用int32_t存储长度可以节省空间,
    // 如果使用两个string_view, 类大小会达到40字节, 对优化不利
    // The key generally does not exceed INT32_MAX, and the storage length can be saved by using
    // int32_t, but if two string_view are used, the class size will reach 40 bytes, which is not
    // conducive to optimization
    struct MultiStringView
    {
        const char* address;
        const char* key;
        int32_t addressLength;
        int32_t keyLength;
    };
    std::variant<EVMKeyView, StringKeyView, MultiStringView> m_view;

public:
    explicit StateKeyView(const StateKey& stateKey)
    {
        std::visit(overloaded{[this](const StateKey::EVMKey& evmStateKey) {
                                  m_view = EVMKeyView{&evmStateKey.address, &evmStateKey.key};
                              },
                       [this](const StateKey::StringKey& stringKey) {
                           m_view = StringKeyView{stringKey.tableKey, stringKey.split};
                       }},
            stateKey.m_key);
    }
    StateKeyView(const evmc_address& address, const evmc_bytes32& key)
      : m_view(EVMKeyView{&address, &key})
    {}
    StateKeyView(std::string_view table, std::string_view key)
      : m_view(MultiStringView{table.data(), key.data(), static_cast<int32_t>(table.size()),
            static_cast<int32_t>(key.size())})
    {}

    friend auto operator<=>(const StateKeyView& lhs, const StateKeyView& rhs)
    {
        return std::visit(
            overloaded{[](const EVMKeyView& lhsView, const EVMKeyView& rhsView) {
                           auto lhsStringView = std::string_view(
                               (const char*)std::addressof(lhsView), sizeof(EVMKeyView));
                           auto rhsStringView = std::string_view(
                               (const char*)std::addressof(rhsView), sizeof(EVMKeyView));
                           return lhsStringView <=> rhsStringView;
                       },
                [](const StringKeyView& lhsView, const StringKeyView& rhsView) {
                    return lhsView.addressKey <=> rhsView.addressKey;
                },
                [](const MultiStringView& lhsView, const MultiStringView& rhsView) {
                    auto lhsTableStringView =
                        std::string_view(lhsView.address, lhsView.addressLength);
                    auto rhsTableStringView =
                        std::string_view(rhsView.address, rhsView.addressLength);

                    auto lhsKeyStringView = std::string_view(lhsView.key, lhsView.keyLength);
                    auto rhsKeyStringView = std::string_view(rhsView.key, rhsView.keyLength);

                    auto result = lhsTableStringView <=> rhsTableStringView;
                    if (std::is_eq(result))
                    {
                        result = lhsKeyStringView <=> rhsKeyStringView;
                    }
                    return result;
                },
                [](auto const& lhsView, auto const& rhsView) {
                    return std::strong_ordering::equivalent;
                }},
            lhs.m_view, rhs.m_view);
    }
    friend bool operator==(const StateKeyView& lhs, const StateKeyView& rhs)
    {
        return std::is_eq(lhs <=> rhs);
    }

    size_t hash() const
    {
        return std::visit(
            overloaded{
                [](const StateKeyView::EVMKeyView& evmKeyView) {
                    auto addressStringView = std::string_view(
                        (const char*)evmKeyView.address->bytes, sizeof(evmKeyView.address->bytes));
                    auto keyStringView = std::string_view(
                        (const char*)evmKeyView.key->bytes, sizeof(evmKeyView.key->bytes));

                    auto result = std::hash<std::string_view>{}(addressStringView);
                    boost::hash_combine(result, std::hash<std::string_view>{}(keyStringView));
                    return result;
                },
                [](const StateKeyView::StringKeyView& stringKeyView) {
                    return std::hash<std::string_view>{}(stringKeyView.addressKey);
                },
                [](const StateKeyView::MultiStringView& multiStringView) {
                    auto addressStringView =
                        std::string_view(multiStringView.address, multiStringView.addressLength);
                    auto keyStringView =
                        std::string_view(multiStringView.key, multiStringView.keyLength);

                    auto result = std::hash<std::string_view>{}(addressStringView);
                    boost::hash_combine(result, std::hash<std::string_view>{}(keyStringView));
                    return result;
                }},
            m_view);
    }
};

}  // namespace bcos::transaction_executor

template <>
struct std::less<bcos::transaction_executor::StateKey>
{
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKeyView const& rightView) const -> bool
    {
        auto leftView = bcos::transaction_executor::StateKeyView(left);
        return leftView < rightView;
    }
    auto operator()(bcos::transaction_executor::StateKeyView const& leftView,
        bcos::transaction_executor::StateKey const& right) const -> bool
    {
        auto rightView = bcos::transaction_executor::StateKeyView(right);
        return leftView < rightView;
    }
    auto operator()(bcos::transaction_executor::StateKey const& lhs,
        bcos::transaction_executor::StateKey const& rhs) const -> bool
    {
        return lhs < rhs;
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKeyView>
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        return stateKeyView.hash();
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKeyView>
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        return stateKeyView.hash();
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        auto view = bcos::transaction_executor::StateKeyView(stateKey);
        return std::hash<bcos::transaction_executor::StateKeyView>{}(view);
    }
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        return stateKeyView.hash();
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        return std::hash<bcos::transaction_executor::StateKey>{}(stateKey);
    }
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        return stateKeyView.hash();
    }
};

inline std::ostream& operator<<(
    std::ostream& stream, const bcos::transaction_executor::StateKey& stateKey)
{
    auto myStateKey = stateKey;
    myStateKey.convertToStringKey();
    stream << myStateKey.getStringKey();
    return stream;
}

template <>
struct std::equal_to<bcos::transaction_executor::StateKey>
{
    bool operator()(bcos::transaction_executor::StateKey const& lhs,
        bcos::transaction_executor::StateKey const& rhs) const
    {
        return std::is_eq(lhs <=> rhs);
    }
    bool operator()(bcos::transaction_executor::StateKeyView const& lhsView,
        bcos::transaction_executor::StateKey const& rhs) const
    {
        auto rhsView = bcos::transaction_executor::StateKeyView(rhs);
        return std::is_eq(lhsView <=> rhsView);
    }
};