#pragma once
#include "../storage/Entry.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/ThreeWay4Apple.h"
#include <boost/throw_exception.hpp>
#include <compare>
#include <functional>
#include <string_view>

namespace bcos::executor_v1
{
DERIVE_BCOS_EXCEPTION(NoTableSpliterError);

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
            throwTrace(NoTableSpliterError());
        }
    }
    explicit StateKey(StateKeyView const& view);

    StateKey(const StateKey&) = default;
    StateKey(StateKey&&) noexcept = default;
    StateKey& operator=(const StateKey&) = default;
    StateKey& operator=(StateKey&&) noexcept = default;
    ~StateKey() noexcept = default;

    friend ::std::ostream& operator<<(
        ::std::ostream& stream, const bcos::executor_v1::StateKey& stateKey)
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
    StateKeyView& operator=(const StateKeyView&) noexcept = default;
    StateKeyView& operator=(StateKeyView&&) noexcept = default;
    StateKeyView(const StateKeyView& stateKeyView) noexcept = default;
    explicit StateKeyView(const StateKey& stateKey) noexcept
      : m_table(stateKey.data(), stateKey.m_split),
        m_key(stateKey.data() + stateKey.m_split + 1, stateKey.size() - stateKey.m_split - 1)
    {}
    StateKeyView(std::string_view table, std::string_view key) noexcept : m_table(table), m_key(key)
    {}
    ~StateKeyView() noexcept = default;

    friend auto operator<=>(const StateKeyView& lhs, const StateKeyView& rhs) noexcept = default;
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

    std::tuple<std::string_view, std::string_view> get() const noexcept { return {m_table, m_key}; }
};

inline StateKey::StateKey(StateKeyView const& view) : StateKey(view.m_table, view.m_key) {}

inline std::strong_ordering operator<=>(const StateKey& lhs, const StateKey& rhs) noexcept
{
    auto lhsView = bcos::executor_v1::StateKeyView{lhs};
    auto rhsView = bcos::executor_v1::StateKeyView{rhs};
    return lhsView <=> rhsView;
}
inline bool operator==(const StateKey& lhs, const StateKey& rhs) noexcept
{
    return std::is_eq(lhs <=> rhs);
}

inline std::strong_ordering operator<=>(
    const StateKey& lhs, const bcos::executor_v1::StateKeyView& rhs) noexcept
{
    auto lhsView = bcos::executor_v1::StateKeyView{lhs};
    return lhsView <=> rhs;
}
inline bool operator==(
    const bcos::executor_v1::StateKey& lhs, const bcos::executor_v1::StateKeyView& rhs) noexcept
{
    return std::is_eq(lhs <=> rhs);
}

}  // namespace bcos::executor_v1

template <>
struct std::less<bcos::executor_v1::StateKey>
{
    auto operator()(auto const& lhs, auto const& rhs) const noexcept -> bool { return lhs < rhs; }
};

template <>
struct std::hash<bcos::executor_v1::StateKeyView>
{
    size_t operator()(const bcos::executor_v1::StateKeyView& stateKeyView) const noexcept
    {
        return stateKeyView.hash();
    }
};

template <>
struct boost::hash<bcos::executor_v1::StateKeyView>
  : public std::hash<bcos::executor_v1::StateKeyView>
{
};

template <>
struct std::hash<bcos::executor_v1::StateKey>
{
    size_t operator()(const auto& stateKey) const noexcept
    {
        bcos::executor_v1::StateKeyView view(stateKey);
        return std::hash<bcos::executor_v1::StateKeyView>{}(view);
    }
};

template <>
struct boost::hash<bcos::executor_v1::StateKey> : public std::hash<bcos::executor_v1::StateKey>
{
};

template <>
struct std::equal_to<bcos::executor_v1::StateKey>
{
    bool operator()(auto const& lhs, auto const& rhs) const noexcept { return lhs == rhs; }
};
