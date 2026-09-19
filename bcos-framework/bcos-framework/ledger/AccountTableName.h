#pragma once

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>

/// Account table names have two physical encodings of the same logical table:
///   hex:    "/apps/" + 40 lowercase hex chars of the 20-byte address (the legacy layout)
///   binary: "/apps/" + the 20 raw address bytes          (the raw-address layout)
///
/// The encoding is a NODE-LOCAL physical layout choice, not a chain property: the XOR state
/// root normalizes binary names back to hex (canonicalTableNameForHash below, via
/// Entry::hash) and the MPT leaf key is keccak(address), so two nodes holding the same
/// logical state in different encodings commit identical roots. feature_raw_address
/// (Features.h Flag=54) used to gate this; it is deprecated and drives nothing now.
///
/// This header is deliberately dependency-free (no ledger/LedgerTypeDef.h): the prefix is
/// a literal mirror of ledger::SYS_DIRECTORY::USER_APPS, the same arrangement StateKey.h
/// documents — LedgerTypeDef.h pulls in StateKey.h/Storage.h, and this header is included
/// from storage/Entry.cpp, so naming the constant here would drag the world into every
/// Entry translation unit for no benefit.
namespace bcos::ledger::account
{
inline constexpr std::string_view APPS_PREFIX = "/apps/";  // ledger::SYS_DIRECTORY::USER_APPS
inline constexpr size_t ADDRESS_SIZE = 20;                 // bcos::Address::SIZE
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

/// "/apps/" + exactly 20 bytes (any byte values). The two encodings are distinguished by
/// length alone: 6+20 vs 6+40 are mutually exclusive, so no byte-level ambiguity exists.
inline bool isBinaryAccountTableName(std::string_view table) noexcept
{
    return table.size() == APPS_PREFIX.size() + ADDRESS_SIZE && table.starts_with(APPS_PREFIX);
}

constexpr char HEX_DIGITS[] = "0123456789abcdef";

/// binary "/apps/<20 bytes>" → hex "/apps/<40 lowercase hex>". Returns an empty string
/// for input that is not a binary account table name (caller error; see the is* probes).
inline std::string binaryToHexAccountTableName(std::string_view table)
{
    if (!isBinaryAccountTableName(table))
    {
        return {};
    }
    std::string result;
    result.reserve(APPS_PREFIX.size() + HEX_ADDRESS_SIZE);
    result.append(APPS_PREFIX);
    for (size_t i = APPS_PREFIX.size(); i < table.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(table[i]);
        result.push_back(HEX_DIGITS[byte >> 4]);
        result.push_back(HEX_DIGITS[byte & 0x0f]);
    }
    return result;
}

/// hex "/apps/<40 lowercase hex>" → binary "/apps/<20 bytes>". Returns an empty string
/// for input that is not a hex account table name (caller error; see the is* probes).
inline std::string hexToBinaryAccountTableName(std::string_view table)
{
    if (!isHexAccountTableName(table))
    {
        return {};
    }
    auto nibble = [](char c) -> char {
        return c <= '9' ? static_cast<char>(c - '0') : static_cast<char>(c - 'a' + 10);
    };
    std::string result;
    result.reserve(APPS_PREFIX.size() + ADDRESS_SIZE);
    result.append(APPS_PREFIX);
    for (size_t i = APPS_PREFIX.size(); i < table.size(); i += 2)
    {
        result.push_back(static_cast<char>((nibble(table[i]) << 4) | nibble(table[i + 1])));
    }
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
///   - Binary: "/apps/<20 raw address bytes>" — the raw-address layout (migrated chains and
///     new chains on a mode-aware lane).
///   - BinaryWithHexFallback: Binary for every WRITE and the primary read, plus a read
///     fallback to the Hex table. Every write also DELETES the hex twin row (write-time
///     dedup): under the encoding-agnostic Entry::hash a logical row present in both
///     encodings would fold into the XOR state root twice, so writes pin it to one copy.
///     This is the mid-migration layout: rows not yet touched still live in hex tables.
enum class AddressTableMode
{
    Hex,
    Binary,
    BinaryWithHexFallback,
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
}  // namespace bcos::ledger::account
