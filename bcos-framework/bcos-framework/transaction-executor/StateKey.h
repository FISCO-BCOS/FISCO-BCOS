#pragma once
#include "../storage/Entry.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/ThreeWay4StringView.h"
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>

namespace bcos::transaction_executor
{
struct NoTableSpliterError : public bcos::Exception
{
};

using StateValue = storage::Entry;
class StateKeyView;

class StateKey
{
public:
    std::string m_tableAndKey;
    size_t m_split{};

    StateKey() = default;
    StateKey(std::string_view table, std::string_view key) : m_split(table.size())
    {
        m_tableAndKey.reserve(table.size() + 1 + key.size());
        m_tableAndKey.append(table);
        m_tableAndKey.push_back(':');
        m_tableAndKey.append(key);
    }
    explicit StateKey(std::string tableAndKey)
      : m_tableAndKey(std::move(tableAndKey)), m_split(m_tableAndKey.find_first_of(':'))
    {
        if (m_split == std::string::npos)
        {
            BOOST_THROW_EXCEPTION(NoTableSpliterError());
        }
    }
    explicit StateKey(StateKeyView const& view);

    StateKey(const StateKey&) = default;
    StateKey(StateKey&&) noexcept = default;
    StateKey& operator=(const StateKey&) = default;
    StateKey& operator=(StateKey&&) noexcept = default;
    ~StateKey() noexcept = default;

    friend bool operator==(const StateKey& lhs, const StateKey& rhs) = default;
    friend ::std::ostream& operator<<(
        ::std::ostream& stream, const bcos::transaction_executor::StateKey& stateKey)
    {
        stream << stateKey.m_tableAndKey;
        return stream;
    }
    const char* data() const& noexcept { return m_tableAndKey.data(); }
    size_t size() const noexcept { return m_tableAndKey.size(); }
};

class StateKeyView
{
public:
    std::string_view m_table;
    std::string_view m_key;
    friend class StateKey;

    StateKeyView(StateKeyView&&) noexcept = default;
    StateKeyView& operator=(const StateKeyView&) = default;
    StateKeyView& operator=(StateKeyView&&) noexcept = default;
    StateKeyView(const StateKeyView& stateKeyView) noexcept = default;
    explicit StateKeyView(const StateKey& stateKey) noexcept
      : m_table(stateKey.data(), stateKey.m_split),
        m_key(stateKey.data() + stateKey.m_split + 1, stateKey.size() - stateKey.m_split - 1)
    {}
    StateKeyView(std::string_view table, std::string_view key) noexcept : m_table(table), m_key(key)
    {}
    ~StateKeyView() noexcept = default;

    friend std::strong_ordering operator<=>(
        const StateKeyView& lhs, const StateKeyView& rhs) noexcept
    {
        auto cmp = lhs.m_table <=> rhs.m_table;
        if (std::is_eq(cmp))
        {
            cmp = lhs.m_key <=> rhs.m_key;
        }
        return cmp;
    }

    friend bool operator==(const StateKeyView& lhs, const StateKeyView& rhs) noexcept = default;
    friend ::std::ostream& operator<<(::std::ostream& stream, const StateKeyView& stateKeyView)
    {
        stream << stateKeyView.m_table << ":" << stateKeyView.m_key;
        return stream;
    }

    size_t hash() const noexcept
    {
        auto result = std::hash<std::string_view>{}(m_table);
        boost::hash_combine(result, std::hash<std::string_view>{}(m_key));
        return result;
    }

    std::tuple<std::string_view, std::string_view> getTableAndKey() const noexcept
    {
        return {m_table, m_key};
    }
};

inline StateKey::StateKey(StateKeyView const& view) : StateKey(view.m_table, view.m_key) {}

template <class RHS>
inline bool operator==(const bcos::transaction_executor::StateKey& lhs, const RHS& rhs) noexcept
    requires std::same_as<std::decay_t<RHS>, bcos::transaction_executor::StateKey> ||
             std::same_as<std::decay_t<RHS>, bcos::transaction_executor::StateKeyView>
{
    auto lhsView = bcos::transaction_executor::StateKeyView(lhs);
    if constexpr (std::same_as<RHS, bcos::transaction_executor::StateKey>)
    {
        return std::is_eq(lhsView <=> bcos::transaction_executor::StateKeyView(rhs));
    }
    else
    {
        return std::is_eq(lhsView <=> rhs);
    }
}

template <class RHS>
inline std::strong_ordering operator<=>(const StateKey& lhs, const RHS& rhs) noexcept
    requires std::same_as<std::decay_t<RHS>, bcos::transaction_executor::StateKey> ||
             std::same_as<std::decay_t<RHS>, bcos::transaction_executor::StateKeyView>
{
    auto lhsView = bcos::transaction_executor::StateKeyView(lhs);
    if constexpr (std::same_as<RHS, bcos::transaction_executor::StateKey>)
    {
        return lhsView <=> bcos::transaction_executor::StateKeyView(rhs);
    }
    else
    {
        return lhsView <=> rhs;
    }
}

}  // namespace bcos::transaction_executor

template <>
struct std::less<bcos::transaction_executor::StateKey>
{
    auto operator()(auto const& lhs, auto const& rhs) const noexcept -> bool
    {
        bcos::transaction_executor::StateKeyView lhsView(lhs);
        bcos::transaction_executor::StateKeyView rhsView(rhs);
        return lhsView < rhsView;
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKeyView>
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const noexcept
    {
        return stateKeyView.hash();
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKeyView>
  : public std::hash<bcos::transaction_executor::StateKeyView>
{
};

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const auto& stateKey) const noexcept
    {
        bcos::transaction_executor::StateKeyView view(stateKey);
        return std::hash<bcos::transaction_executor::StateKeyView>{}(view);
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKey>
  : public std::hash<bcos::transaction_executor::StateKey>
{
};

template <>
struct std::equal_to<bcos::transaction_executor::StateKey>
{
    bool operator()(auto const& lhs, auto const& rhs) const noexcept
    {
        bcos::transaction_executor::StateKeyView lhsView(lhs);
        bcos::transaction_executor::StateKeyView rhsView(rhs);
        return lhsView == rhsView;
    }
};
