#pragma once
#include "../storage/Entry.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/Overloaded.h"
#include "bcos-utilities/Ranges.h"
#include "bcos-utilities/ThreeWay4StringView.h"
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>
#include <variant>

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

    friend std::strong_ordering operator<=>(const StateKey& lhs, const StateKey& rhs) = default;
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

    explicit StateKeyView(const StateKey& stateKey) noexcept
      : m_table(stateKey.data(), stateKey.m_split),
        m_key(stateKey.data() + stateKey.m_split + 1, stateKey.size() - stateKey.m_split - 1)
    {}
    StateKeyView(std::string_view table, std::string_view key) noexcept : m_table(table), m_key(key)
    {}

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
    friend std::strong_ordering operator<=>(const StateKeyView& lhs, const StateKey& rhs) noexcept
    {
        StateKeyView rhsView(rhs);
        return lhs <=> rhsView;
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

}  // namespace bcos::transaction_executor

inline std::ostream& operator<<(
    std::ostream& stream, const bcos::transaction_executor::StateKey& stateKey)
{
    bcos::transaction_executor::StateKeyView view(stateKey);
    stream << view.m_table << "-" << bcos::toHex(view.m_key);
    return stream;
}

template <>
struct std::less<bcos::transaction_executor::StateKey>
{
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKeyView const& rightView) const noexcept -> bool
    {
        return left < rightView;
    }
    auto operator()(bcos::transaction_executor::StateKeyView const& leftView,
        bcos::transaction_executor::StateKey const& right) const noexcept -> bool
    {
        return leftView < right;
    }
    auto operator()(bcos::transaction_executor::StateKey const& lhs,
        bcos::transaction_executor::StateKey const& rhs) const noexcept -> bool
    {
        return lhs < rhs;
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
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const noexcept
    {
        return stateKeyView.hash();
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const noexcept
    {
        auto view = bcos::transaction_executor::StateKeyView(stateKey);
        return std::hash<bcos::transaction_executor::StateKeyView>{}(view);
    }
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const noexcept
    {
        return stateKeyView.hash();
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const noexcept
    {
        return std::hash<bcos::transaction_executor::StateKey>{}(stateKey);
    }
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const noexcept
    {
        return stateKeyView.hash();
    }
};

template <>
struct std::equal_to<bcos::transaction_executor::StateKey>
{
    bool operator()(bcos::transaction_executor::StateKey const& lhs,
        bcos::transaction_executor::StateKey const& rhs) const noexcept
    {
        return std::is_eq(lhs <=> rhs);
    }
    bool operator()(bcos::transaction_executor::StateKeyView const& lhsView,
        bcos::transaction_executor::StateKey const& rhs) const noexcept
    {
        auto rhsView = bcos::transaction_executor::StateKeyView(rhs);
        return std::is_eq(lhsView <=> rhsView);
    }
};
