// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Pure static helpers for the Storage2State block bridge (split out of Storage2State.h).
// None of these touch the Storage template parameter — they are plain free functions in
// `bcos::evm::evmstate`, resolvable by ordinary lookup from inside the class body.
// Account field classification follows the same key set as the mainline MPT path.

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Overloaded.h>
#include <algorithm>
#include <cstring>
#include <evmc/evmc.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace bcos::evm::evmstate
{
/// Fixed length (32 bytes) of a raw storage-slot key in an account table, used to distinguish
/// slot keys (evmc::bytes32) from the known short field names in ACCOUNT_TABLE_FIELDS during the
/// has_storage range-seek probe.
inline constexpr std::size_t kStorageSlotKeySize = sizeof(evmc_bytes32::bytes);

/// Value-variant discrimination (Critical): storage2 logical deletion means range() merges do
/// NOT filter tombstones — a raw range item's value is the full
/// `storage2::StorageValueType<Value>` variant (NOT_EXISTS_TYPE/DELETED_TYPE/Value), and the
/// traversal must skip the two tombstone alternatives itself, or a block-internal delete
/// would resurrect into the state root. Returns the live content view, or nullopt for a
/// tombstone (either alternative) — the caller never needs to distinguish "never existed"
/// from "logically deleted", both mean "not part of this traversal".
template <class RawValue>
inline std::optional<std::string_view> liveContent(const RawValue& rawValue)
{
    return std::visit(
        bcos::overloaded{[](const storage2::NOT_EXISTS_TYPE&) -> std::optional<std::string_view> {
                             return std::nullopt;
                         },
            [](const storage2::DELETED_TYPE&) -> std::optional<std::string_view> {
                return std::nullopt;
            },
            [](const auto& entry) -> std::optional<std::string_view> { return entry.get(); }},
        rawValue);
}

/// Whether fieldKey is one of the ACCOUNT_TABLE_FIELDS full set
/// (CODE_HASH/CODE/BALANCE/ABI/NONCE/ALIVE/FROZEN/SHARD) — these rows are already read by
/// fetchAccount (or, for CODE, intentionally never read) and must not be misclassified as a
/// 32-byte storage slot key during the account-table range scan. BCOS extension fields
/// (status/last_update/last_status, written by AccountPrecompiled) are also not slots: the
/// mainline MPT classifier treats them as BcosExtension rows and skips them, so the bridge
/// must do the same or fetchAllStorage throws "unknown key in account table" and the whole
/// OP block poisons.
inline bool isKnownAccountField(std::string_view fieldKey)
{
    using Fields = bcos::ledger::ACCOUNT_TABLE_FIELDS;
    return fieldKey == Fields::CODE_HASH || fieldKey == Fields::CODE ||
           fieldKey == Fields::BALANCE || fieldKey == Fields::ABI || fieldKey == Fields::NONCE ||
           fieldKey == Fields::ALIVE || fieldKey == Fields::FROZEN || fieldKey == Fields::SHARD ||
           bcos::ledger::mpt::isKnownBcosExtensionField(fieldKey);
}

/// Zero-valued slot rule: under Ethereum semantics "slot value == 0 ≡ slot does not exist",
/// so a 32-byte all-zero row must be treated as absent by every predicate. Content that is not
/// exactly 32 bytes is conservatively treated as present (refuse CREATE on it in the EIP-7610
/// direction, not silently demoted to "zero/absent").
inline bool isZeroSlotValue(std::string_view content) noexcept
{
    return content.size() == sizeof(evmc_bytes32::bytes) &&
           std::all_of(content.begin(), content.end(), [](char byte) { return byte == '\0'; });
}

/// Decodes the address embedded in a "/apps/<hex(addr)>" SYS_TABLES key, or nullopt when the
/// table is not an account table at all. Reuses the mainline MPT classifier
/// `bcos::ledger::mpt::parseAccountTable` (Classify.h) rather than reimplementing the rule —
/// the `/apps/` namespace also holds non-account tables (authorization/BFS link tables). One
/// classifier keeps mainline MPT root and OP stateRoot agreeing on which tables are accounts.
inline std::optional<evmc::address> addressFromTableName(std::string_view tableKey)
{
    auto parsed = bcos::ledger::mpt::parseAccountTable(tableKey);
    if (!parsed.has_value())
        return std::nullopt;
    evmc::address addr{};
    static_assert(sizeof(addr.bytes) == bcos::Address::SIZE,
        "evmc::address and bcos::Address must both be 20 bytes for this memcpy");
    std::memcpy(addr.bytes, parsed->data(), sizeof(addr.bytes));
    return addr;
}

/// Account table path: unconditionally "/apps/" + hex_lower(addr). Delegates to the mainline MPT
/// classifier (Classify.h) so the `/apps/` prefix rule has a single home — same shape as the
/// mainline `accountTableName` and strictly inverse to `parseAccountTable`.
///
/// The 8 `c_systemTxsAddress` addresses are ORDINARY accounts here and must be collected
/// unconditionally — ordinary addresses on the Ethereum side. Current semantics (three answers,
/// all identical): read from `/apps/<40hex>` (missing -> nullopt = Ethereum empty account);
/// write to the same `/apps/<40hex>` (bypass EVMAccount's own /sys/ routing); enters the
/// stateRoot iff that table exists. FISCO's own `/sys/<40hex>` control plane stays invisible to
/// the OP execution world.
inline std::string accountTableName(const evmc::address& addr)
{
    // bytesConstRef 构造默认 AlignRight——20 字节地址恰为 20 字节，对齐不影响结果。
    return bcos::ledger::mpt::accountTableName(
        bcos::Address{bcos::bytesConstRef{addr.bytes, sizeof(addr.bytes)}});
}
}  // namespace bcos::evm::evmstate
