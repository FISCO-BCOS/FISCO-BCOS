#pragma once
#include "../storage/Entry.h"
#include "bcos-framework/ledger/AccountTableName.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
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
      : m_tableAndKey(std::move(tableAndKey)), m_split(splitPosition(m_tableAndKey))
    {
        if (m_split == std::string::npos)
        {
            throwTrace(NoTableSpliterError());
        }
    }

    // Locate the table/key separator in the flat "table:key" form. Raw-address
    // account tables (the binary node-local layout: "/s/" + 20 raw address bytes,
    // ledger/account/AccountTableName.h) can contain 0x3a (':') inside the address,
    // so a plain find_first_of(':') would split inside the table name. The binary
    // form is fixed-length, and a ':' at exactly that offset is unambiguous:
    // "/s/" is a reserved namespace that only ever holds the 20-byte binary account
    // tables (BFS cannot create "/s/" tables — checkPathPrefixValid whitelists only
    // "/apps/", "/tables/", "/usr/"), so nothing else places a ':' there by
    // coincidence. Everything else — including ALL "/apps/" tables — keeps
    // first-':' semantics unconditionally.
    //
    // The earlier draft put the binary tables under "/apps/" and needed a fixed-offset
    // rule there too, which had a known ambiguity: a short "/apps/" table whose key
    // happened to place a ':' at the binary split offset was misread as a
    // binary-address table. Moving the binary layout to "/s/" removes that whole
    // class — and nothing binary ever shipped under "/apps/" (feature_raw_address
    // never reached a release; this PR is unmerged), so no committed key needs the
    // old rule and it is deleted outright rather than kept for compatibility.
    //
    // Constants: the 20 is bcos::Address::SIZE; the "/s/" prefix is
    // ledger::account::BINARY_TABLE_PREFIX — that header is dependency-free
    // (ledger/AccountTableName.h), so this header names the shared constant directly
    // instead of mirroring the literal (Classify.h, which keeps no bcos-framework
    // dependency, is the one remaining mirror).
    static size_t splitPosition(std::string_view tableAndKey) noexcept
    {
        constexpr std::string_view binaryTablePrefix = ledger::account::BINARY_TABLE_PREFIX;
        constexpr size_t rawAddressTableSize = binaryTablePrefix.size() + bcos::Address::SIZE;
        if (tableAndKey.size() > rawAddressTableSize &&
            tableAndKey.starts_with(binaryTablePrefix) && tableAndKey[rawAddressTableSize] == ':')
        {
            return rawAddressTableSize;
        }
        return tableAndKey.find_first_of(':');
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
