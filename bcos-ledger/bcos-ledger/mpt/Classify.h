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
 * @brief Flat-state KEY parsing: accountTableName / parseAccountTable / classifyRowKey (spec §5.2,
 *        Revision 2026-07-09b). Key-only on purpose — the old classify() copied every changed
 *        value into an AccountDelta/MPTBuildInput layer the builder then consumed; the block's
 *        delta already sits in the fork view's ordered mutable storage, so MPTBuilder now
 *        range-reads values in place and the value-copy layer is deleted (design3 §4.1 rule 6).
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
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
// with a single ':' separator (StateKey.h:24-30, find_first_of(':') at line 32).
// For an account/contract the table is "/apps/<40-hex-address>" (executor
// Common.h:73 USER_APPS_PREFIX, TransactionExecutive::getContractTableName), and
// the row <key> is one of the field-name strings below (Common.h:79-85,99) or a
// 32-byte binary storage slot (HostContext::setStore writes a 32-byte evmc key).
//
// Ethereum core fields  : "nonce", "balance", "codeHash"
// BCOS extension fields : "abi", "alive", "frozen", "shard", "status",
//                         "last_update", "last_status"  (non-Ethereum rows)
// Code                  : "code" (enters the MPT indirectly via its paired codeHash row)
// Storage slot          : a 32-byte binary row key (length 32, not a field name)
//
// The table prefix "/apps/" contains no ':' so the first ':' is always the
// StateKey separator; we split there.

inline constexpr std::string_view APPS_TABLE_PREFIX = "/apps/";
inline constexpr size_t ADDRESS_HEX_LEN = 40;  // 20-byte address as hex

inline constexpr std::string_view ROW_NONCE = "nonce";
inline constexpr std::string_view ROW_BALANCE = "balance";
inline constexpr std::string_view ROW_CODE_HASH = "codeHash";
inline constexpr std::string_view ROW_CODE = "code";

/// The flat table name of an account: "/apps/" + 40 lowercase hex chars (no 0x).
inline std::string accountTableName(bcos::Address const& addr)
{
    std::string table;
    table.reserve(APPS_TABLE_PREFIX.size() + ADDRESS_HEX_LEN);
    table.append(APPS_TABLE_PREFIX);
    table.append(addr.hex());  // toHex uses hex_lower: 40 lowercase chars
    return table;
}

/// Parse "/apps/<40-hex-address>" into the address. Anything else — system tables (/sys/,
/// /tables/, _accessAuth), BCOS private short-name contracts (table suffix not 40-hex) — is
/// nullopt: those rows never enter the MPT. Never throws.
inline std::optional<bcos::Address> parseAccountTable(std::string_view table)
{
    if (!table.starts_with(APPS_TABLE_PREFIX))
    {
        return std::nullopt;
    }
    std::string_view const addrHex = table.substr(APPS_TABLE_PREFIX.size());
    if (addrHex.size() != ADDRESS_HEX_LEN)
    {
        return std::nullopt;
    }
    // Address (FixedBytes<20>) has a string_view + FromHex constructor. It throws on a non-hex
    // digit; real /apps/ table names are always valid 40-hex, but guard so this parser's
    // no-throw contract holds for arbitrary input.
    try
    {
        return bcos::Address(addrHex, bcos::Address::FromHex);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/// Row-level kinds inside one account's table, consumed by MPTBuilder's prefix-range loop
/// (spec §5.2). A 32-byte binary row key is always a storage slot (no field name is 32 bytes);
/// any named row that is not a core field and not "code" is a BCOS extension — the builder skips
/// those on a native chain and throws UnexpectedBCOSFieldInL2 on an Ethereum-compatible one
/// (loud on purpose: an unrecognized row in scenario B would otherwise silently fall out of the
/// state commitment).
enum class RowKind : uint8_t
{
    Nonce,
    Balance,
    CodeHash,
    Code,           ///< skipped in both modes: represented by the paired codeHash row
    BcosExtension,  ///< abi/alive/frozen/shard/status/... — scenario A skip, scenario B throw
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
    return RowKind::BcosExtension;
}

}  // namespace bcos::ledger::mpt
