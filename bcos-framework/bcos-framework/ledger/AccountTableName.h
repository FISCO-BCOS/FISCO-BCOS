#pragma once

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/hex.hpp>
#include <iterator>

/// Account table names have two physical encodings of the same logical table:
///   hex:    "/apps/" + 40 lowercase hex chars of the 20-byte address (the legacy layout)
///   binary: "/s/" + the 20 raw address bytes              (the raw-address layout)
///
/// The binary encoding lives in its own reserved "/s/" namespace, NOT under "/apps/", so
/// the two encodings are distinguished by PREFIX, never by length alone. An earlier draft
/// put the binary form at "/apps/" + 20 raw bytes, classified by length alone — and any
/// user-created BFS table under "/apps/" with a 20-char name (mkdir / link / CNS can
/// produce those) was then misread as a binary account table (boot-detection false
/// positives, MPT-scan halts). "/s/" eliminates that class rather than mitigating it:
/// nothing BFS can produce ever starts with "/s/" — BFSPrecompiled::checkPathPrefixValid
/// (bcos-executor/src/precompiled/BFSPrecompiled.cpp) whitelists only "/apps/", "/tables/"
/// and "/usr/" (sharding forces "/shards/", TableManager forces "/tables/") — and no code
/// in the repo constructs "/s/"-prefixed names for anything else. Sort order is
/// undisturbed: "/s/" lands between "/mpt/" and "/shards/" / "/sys/", and every existing
/// range scan classifies rows by in-loop prefix checks, not hardcoded end-bounds.
///
/// The encoding is a NODE-LOCAL physical layout choice, not a chain property: the XOR state
/// root normalizes binary names back to hex (Entry::hash, via binaryToHexAccountTableName)
/// and the MPT leaf key is keccak(address), so two nodes holding the same logical state in
/// different encodings commit identical roots. feature_raw_address
/// (Features.h Flag=54) used to gate this; it is deprecated and drives nothing now.
///
/// There is no runtime mixed mode: a node is either all-hex or all-binary in the steady
/// state. A mixed layout on disk means an interrupted hex→binary migration and is resolved
/// at BOOT — resume the migration ([storage] migrate_account_tables_to_binary) or refuse to
/// start — never by falling back between tables per read. The migration's progress is NOT
/// inferred from the registration rows (the /apps/ account rows rename before the first
/// s_tables:/apps/ registration, so a crash in the account-row phase still scans as pure
/// hex); it is tracked by a flag key inside the state DB itself
/// (s_node_local:account_table_layout: "migrating" synced before the first batch, "bin"
/// written inside the final synced batch — atomically with the data, so DB-level
/// checkpoints and backups carry the state machine). A chain that predates the mechanism
/// has no flag, which IS the hex verdict — boot detection is a single point Get, no
/// registration scan. See libinitializer/AddressTableModeDetection.h and
/// AccountTableMigration.h.
///
/// This header keeps no dependency on the heavy ledger headers (LedgerTypeDef.h pulls in
/// StateKey.h/Storage.h): the hex prefix is a literal mirror of ledger::SYS_DIRECTORY::USER_APPS,
/// and this header must stay includable from leaf contexts that avoid that weight
/// (transaction-executor/StateKey.h references BINARY_TABLE_PREFIX from here; bcos-ledger's
/// Classify.h keeps no bcos-framework dependency at all and therefore keeps its own literal
/// mirror). bcos-utilities/FixedBytes.h (bcos::Address) is a leaf utility and fine.
/// "/s/" has no LedgerTypeDef counterpart at all: it is a reserved
/// namespace owned by this header alone.
namespace bcos::ledger::account
{
inline constexpr std::string_view APPS_PREFIX = "/apps/";  // ledger::SYS_DIRECTORY::USER_APPS
inline constexpr std::string_view BINARY_TABLE_PREFIX = "/s/";  // reserved, see above
inline constexpr size_t ADDRESS_SIZE = 20;                      // bcos::Address::SIZE
inline constexpr size_t HEX_ADDRESS_SIZE = ADDRESS_SIZE * 2;

/// "/apps/" + exactly 40 [0-9a-f] chars. Uppercase hex is NOT accepted: the canonical
/// form is lowercase (EVMAccount generates names with boost::algorithm::hex_lower), so an
/// uppercase name is a different (invalid) table, not another encoding of this one.
inline bool isHexAccountTableName(std::string_view table) noexcept
{
    return table.size() == APPS_PREFIX.size() + HEX_ADDRESS_SIZE &&
           table.starts_with(APPS_PREFIX) &&
           std::all_of(table.begin() + APPS_PREFIX.size(), table.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

/// "/s/" + exactly 20 bytes (any byte values). Prefix-based, not length-based: a
/// "/apps/" table with a 20-char name is a plain BFS table, never a binary account
/// table — the "/s/" namespace is unreachable for BFS (see the header doc), so this
/// probe can never misfire on user-created tables.
inline bool isBinaryAccountTableName(std::string_view table) noexcept
{
    return table.size() == BINARY_TABLE_PREFIX.size() + ADDRESS_SIZE &&
           table.starts_with(BINARY_TABLE_PREFIX);
}

/// binary "/s/<20 bytes>" → hex "/apps/<40 lowercase hex>". Returns an empty string
/// for input that is not a binary account table name (caller error; see the is* probes).
/// boost::algorithm::hex_lower/unhex are header-only — no include cycle, so the codec is
/// shared with every other hex user (EVMAccount.h) instead of hand-rolled here.
inline std::string binaryToHexAccountTableName(std::string_view table)
{
    if (!isBinaryAccountTableName(table))
    {
        return {};
    }
    std::string result;
    result.reserve(APPS_PREFIX.size() + HEX_ADDRESS_SIZE);
    result.append(APPS_PREFIX);
    boost::algorithm::hex_lower(
        table.begin() + BINARY_TABLE_PREFIX.size(), table.end(), std::back_inserter(result));
    return result;
}

/// hex "/apps/<40 lowercase hex>" → binary "/s/<20 bytes>". Returns an empty string
/// for input that is not a hex account table name (caller error; see the is* probes).
inline std::string hexToBinaryAccountTableName(std::string_view table)
{
    if (!isHexAccountTableName(table))
    {
        return {};
    }
    std::string result;
    result.reserve(BINARY_TABLE_PREFIX.size() + ADDRESS_SIZE);
    result.append(BINARY_TABLE_PREFIX);
    // isHexAccountTableName validated lowercase hex of even length, so unhex cannot throw.
    boost::algorithm::unhex(
        table.begin() + APPS_PREFIX.size(), table.end(), std::back_inserter(result));
    return result;
}

/// The canonical table name for state-root hashing: binary account table names map to
/// their hex form; everything else (hex account names, /sys/ tables, s_tables, short-name
/// tables, "/apps/<40hex>_accessAuth" auth tables — auth tables are NOT migrated and NOT
/// normalized) passes through unchanged.
///
/// Normalization makes the XOR state root a function of the logical state alone: a
/// migrated node (binary tables), an unmigrated node (hex tables) and a mixed layout all
/// fold the same digest for the same logical rows. No binary table name exists in
/// committed history before this merged (feature_raw_address never shipped on any chain),
/// so from the moment this merges this is the ONLY semantic — deliberately no feature gate.
///
/// Entry::hash does NOT call this helper: it needs the copy only for binary names (an
/// unconditional std::string copy of a 46-char hex name exceeds SSO and would allocate on
/// every call), so it inlines the isBinaryAccountTableName + binaryToHexAccountTableName
/// pair. This remains the convenience form for callers that always want an owning string.
inline std::string canonicalTableNameForHash(std::string_view table)
{
    if (isBinaryAccountTableName(table))
    {
        return binaryToHexAccountTableName(table);
    }
    return std::string(table);
}

/// How an account's table name is derived from its address on THIS node — a node-local
/// physical layout, deliberately independent of the chain's feature set:
///   - Hex: "/apps/<40 lowercase hex chars>" — the legacy layout (existing chains default).
///   - Binary: "/s/<20 raw address bytes>" — the raw-address layout (migrated chains and
///     new chains on a mode-aware lane).
/// A node is in exactly one of the two: migration between them is a one-shot boot-time
/// rewrite (libinitializer/AccountTableMigration), not a runtime mode.
enum class AddressTableMode
{
    Hex,
    Binary,
};

namespace detail
{
inline std::atomic<AddressTableMode> g_nodeAddressTableMode{AddressTableMode::Hex};
}

/// Publish this node's account-table mode. Startup sets it ONCE, single-threaded, before
/// any executor/scheduler/RPC service starts (libinitializer's storage-init path); every
/// read afterwards is concurrent and read-only. Libraries and unit tests that never run
/// the startup flow keep the Hex default — exactly the pre-detection behavior, so tests
/// need no setup.
inline void setNodeAddressTableMode(AddressTableMode mode) noexcept
{
    detail::g_nodeAddressTableMode.store(mode, std::memory_order_relaxed);
}

/// This node's account-table mode (see setNodeAddressTableMode). Relaxed load: the value
/// is published before the readers start and never changes afterwards.
inline AddressTableMode nodeAddressTableMode() noexcept
{
    return detail::g_nodeAddressTableMode.load(std::memory_order_relaxed);
}

/// The hex-layout account table name of an address: "/apps/" + 40 lowercase hex chars, with
/// NO /sys/ routing — in the Ethereum execution world (the OP lane bridge, the Eth
/// executor's state view, the PoW reward path) the c_systemTxsAddress members are ordinary
/// accounts and must NOT be rerouted. This is the single home of that rule; contrast with
/// EVMAccount.h's accountTableName, which is mode-aware and routes system addresses to
/// /sys/. The binary layout deliberately has no producer here: the Ethereum lanes are
/// hex-only (libinitializer refuses binary account data on them), and making this helper
/// mode-aware is exactly the follow-up that requires it to live in bcos-framework rather
/// than in bcos-ledger's Classify.h (which keeps no bcos-framework dependency).
inline std::string hexAccountTableName(bcos::Address const& addr)
{
    std::string table;
    table.reserve(APPS_PREFIX.size() + HEX_ADDRESS_SIZE);
    table.append(APPS_PREFIX);
    table.append(addr.hex());  // FixedBytes::hex uses hex_lower: 40 lowercase chars
    return table;
}
}  // namespace bcos::ledger::account
