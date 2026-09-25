/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Classify.h
 * @brief Flat-state KEY parsing: parseAccountTable / classifyRowKey (spec §5.2,
 *        Revision 2026-07-09b). Key-only on purpose — the old classify() copied every changed
 *        value into an AccountDelta/MPTBuildInput layer the builder then consumed; the block's
 *        delta already sits in the fork view's ordered mutable storage, so MPTBuilder now
 *        range-reads values in place and the value-copy layer is deleted (design3 §4.1 rule 6).
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{

// ---------------------------------------------------------------------------
// Flat-state key format (VERIFIED against the executor's StateKey layout)
// ---------------------------------------------------------------------------
// A flat-state row is keyed by an executor_v1::StateKey, whose serialized form is
//   "<table>:<key>"
// with a single ':' separator (StateKey.h:24-30). For an account/contract the table is
// "/apps/<40-hex-address>" — or, in the binary node-local account-table layout,
// "/s/<20 raw address bytes>" (ledger/account/AccountTableName.h; EVMAccount.h
// AddressTableMode) — and
// the row <key> is one of the field-name strings below (Common.h:79-85,99) or a
// 32-byte binary storage slot (HostContext::setStore writes a 32-byte evmc key).
//
// Ethereum core fields  : "nonce", "balance", "codeHash"
// BCOS extension fields : "abi", "alive", "frozen", "shard", "status",
//                         "last_update", "last_status"  (non-Ethereum rows)
// Code                  : "code" (enters the MPT indirectly via its paired codeHash row)
// Storage slot          : a 32-byte binary row key (length 32, not a field name)
//
// The table/key split is NOT a plain first-':' split under the raw-address layout: the
// "/s/" prefix itself contains no ':', but a 20-byte binary address can hold 0x3a (':')
// verbatim, so the first ':' of "<table>:<key>" may sit INSIDE the table name. The split
// rule lives in StateKey.h splitPosition (a fixed-offset rule for the "/s/" shape); this
// parser only ever sees the already-split table name.

inline constexpr std::string_view APPS_TABLE_PREFIX = "/apps/";
// The binary layout's reserved namespace: "/s/" + the 20 address bytes appended
// verbatim. Literal mirror of ledger::account::BINARY_TABLE_PREFIX
// (bcos-framework/ledger/AccountTableName.h) — this header keeps no bcos-framework
// dependency, the same arrangement as APPS_TABLE_PREFIX above.
inline constexpr std::string_view BINARY_TABLE_PREFIX = "/s/";
inline constexpr size_t ADDRESS_HEX_LEN = 40;  // 20-byte address as hex (legacy layout)
// The binary layout: the 20 address bytes appended verbatim.
inline constexpr size_t ADDRESS_BIN_LEN = bcos::Address::SIZE;

inline constexpr std::string_view ROW_NONCE = "nonce";
inline constexpr std::string_view ROW_BALANCE = "balance";
inline constexpr std::string_view ROW_CODE_HASH = "codeHash";
inline constexpr std::string_view ROW_CODE = "code";

/// Every account-table field name FISCO writes that is deliberately NOT part of the Ethereum
/// four-tuple. Mirrors bcos-executor/src/Common.h:81-98 exactly — keep the two in sync.
///
/// This is a whitelist rather than a catch-all so the builder can tell "a BCOS field we decided
/// to exclude" from "a field nobody has classified yet". The second must not be skipped silently:
/// a row the builder has never heard of is a row whose Ethereum relevance was never judged, and
/// dropping it would remove state from the commitment with no signal. Adding an account row to
/// FISCO therefore requires one line here, which is where that judgement gets recorded.
inline constexpr std::array<std::string_view, 7> KNOWN_BCOS_EXTENSION_FIELDS{
    "abi", "alive", "frozen", "shard", "status", "last_update", "last_status"};

inline bool isKnownBcosExtensionField(std::string_view rowKey)
{
    return std::ranges::find(KNOWN_BCOS_EXTENSION_FIELDS, rowKey) !=
           KNOWN_BCOS_EXTENSION_FIELDS.end();
}

/// Account table names are PRODUCED elsewhere: the mode-aware routing lives in
/// EVMAccount.h (account::accountTableName), and the hex-only "no /sys/ routing" form the
/// Ethereum lanes need lives next to the encoding vocabulary in bcos-framework
/// (account::hexAccountTableName, AccountTableName.h). The MPT layer only ever PARSES
/// table names (parseAccountTable below); it cannot host the producer because this header
/// deliberately keeps no bcos-framework dependency, so it could never become mode-aware
/// here.

/// Parse an account table name into the address. Two layouts are accepted, told apart by
/// PREFIX — never by length alone:
///   - "/s/" + exactly 20 bytes: the binary layout. The bytes ARE the address and are
///     taken verbatim — any byte pattern (including ':' or non-ASCII) is a valid address, so no
///     validation applies;
///   - "/apps/" + exactly 40 hex chars: the legacy layout, hex-decoded.
/// Anything else — system tables (/sys/, /tables/, _accessAuth), a "/apps/" suffix of any
/// other length or with a non-hex digit, a "/s/" name of any other length — is nullopt:
/// those rows never enter the MPT. Never throws.
///
/// The "/s/" namespace is reserved for the binary account tables and is prefix-free from
/// everything BFS can produce: BFSPrecompiled::checkPathPrefixValid whitelists only
/// "/apps/", "/tables/" and "/usr/" (sharding forces "/shards/", TableManager forces
/// "/tables/"), so no user-created table (mkdir / link / CNS) can ever collide with a
/// binary account name. A "/apps/" table with a 20-char name — the BFS link table
/// "/apps/<name>/<version>" when name + '/' + version totals 20 chars, for instance — is
/// an ordinary non-account table here, NOT a misread binary address; the length-alone
/// classification that made that collision possible is gone with the "/apps/<20 bytes>"
/// encoding (which never shipped, so nothing on disk needs the old reading).
inline std::optional<bcos::Address> parseAccountTable(std::string_view table)
{
    if (table.size() == BINARY_TABLE_PREFIX.size() + ADDRESS_BIN_LEN &&
        table.starts_with(BINARY_TABLE_PREFIX))
    {
        std::string_view const suffix = table.substr(BINARY_TABLE_PREFIX.size());
        return bcos::Address{
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(suffix.data()), suffix.size())};
    }
    if (!table.starts_with(APPS_TABLE_PREFIX))
    {
        return std::nullopt;
    }
    std::string_view const suffix = table.substr(APPS_TABLE_PREFIX.size());
    if (suffix.size() != ADDRESS_HEX_LEN)
    {
        return std::nullopt;
    }
    // Address (FixedBytes<20>) has a string_view + FromHex constructor. It throws on a non-hex
    // digit; real /apps/ table names are always valid 40-hex, but guard so this parser's
    // no-throw contract holds for arbitrary input.
    try
    {
        return bcos::Address(suffix, bcos::Address::FromHex);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/// Row-level kinds inside one account's table, consumed by MPTBuilder's prefix-range loop
/// (spec §5.2). A 32-byte binary row key is always a storage slot (no field name is 32 bytes).
///
/// Named rows that are neither a core field nor "code" split two ways, and the split is the
/// point: KNOWN_BCOS_EXTENSION_FIELDS are fields FISCO deliberately keeps out of the Ethereum
/// four-tuple, so scenario A skips them and only scenario B (where no FISCO-private field should
/// exist at all) throws. Anything else has never been classified by anyone and throws in BOTH
/// modes — loud on purpose, because a row that silently falls out of the state commitment is a
/// fork nobody can trace back to its cause.
enum class RowKind : uint8_t
{
    Nonce,
    Balance,
    CodeHash,
    Code,           ///< skipped in both modes: represented by the paired codeHash row
    BcosExtension,  ///< a KNOWN non-Ethereum field — scenario A skip, scenario B throw
    UnknownField,   ///< never classified — throws in both modes
    StorageSlot,
};

inline RowKind classifyRowKey(std::string_view rowKey)
{
    if (rowKey.size() == static_cast<size_t>(bcos::h256::SIZE))
    {
        return RowKind::StorageSlot;
    }
    if (rowKey == ROW_NONCE)
    {
        return RowKind::Nonce;
    }
    if (rowKey == ROW_BALANCE)
    {
        return RowKind::Balance;
    }
    if (rowKey == ROW_CODE_HASH)
    {
        return RowKind::CodeHash;
    }
    if (rowKey == ROW_CODE)
    {
        return RowKind::Code;
    }
    return isKnownBcosExtensionField(rowKey) ? RowKind::BcosExtension : RowKind::UnknownField;
}

}  // namespace bcos::ledger::mpt
