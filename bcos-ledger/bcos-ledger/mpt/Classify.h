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
 * @brief classify(): translate a flat key→Entry delta into per-account AccountDelta
 *        (spec §5.2). Header-only because classify() is a template over the flat-delta
 *        range type (a storage2 range in production, a vector of pairs in tests).
 */
#pragma once

#include "AccountDelta.h"
#include "Errors.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <optional>
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
// BCOS extension fields : "code", "abi", "alive", "frozen", "shard", "status",
//                         "last_update", "last_status"  (non-Ethereum rows)
// Storage slot          : a 32-byte binary row key (length 32, not a field name)
//
// The table prefix "/apps/" contains no ':' so the first ':' is always the
// StateKey separator; we split there.

inline constexpr std::string_view APPS_TABLE_PREFIX = "/apps/";
inline constexpr size_t ADDRESS_HEX_LEN = 40;  // 20-byte address as hex

inline constexpr std::string_view ROW_NONCE = "nonce";
inline constexpr std::string_view ROW_BALANCE = "balance";
inline constexpr std::string_view ROW_CODE_HASH = "codeHash";

namespace detail
{

/// A parsed flat-state key: the account address plus the kind of row it names.
struct ParsedKey
{
    enum class Kind
    {
        Nonce,
        Balance,
        CodeHash,
        StorageSlot,
        BcosExtension,  ///< code/abi/alive/frozen/shard/... — non-Ethereum field
        NotAnAccount    ///< table is not "/apps/<addr>" → skip entirely
    };
    bcos::Address address;
    Kind kind = Kind::NotAnAccount;
    bcos::h256 slot;  ///< valid only when kind==StorageSlot
};

/// Classify a single "<table>:<row>" key. Never throws: a malformed (non-hex) address is
/// reported as NotAnAccount rather than propagating. l2Mode handling of BcosExtension rows is
/// the caller's decision.
inline ParsedKey parseKey(std::string_view fullKey)
{
    ParsedKey parsed;

    auto const colon = fullKey.find_first_of(':');
    if (colon == std::string_view::npos)
    {
        return parsed;  // NotAnAccount: no StateKey separator
    }
    std::string_view const table = fullKey.substr(0, colon);
    std::string_view const row = fullKey.substr(colon + 1);

    // Table must be "/apps/<40-hex-address>".
    if (!table.starts_with(APPS_TABLE_PREFIX))
    {
        return parsed;  // NotAnAccount: /sys/, /tables/, _accessAuth, etc.
    }
    std::string_view const addrHex = table.substr(APPS_TABLE_PREFIX.size());
    if (addrHex.size() != ADDRESS_HEX_LEN)
    {
        return parsed;  // NotAnAccount: not a 20-byte address table
    }
    // Address(..., FromHex) throws BadHexCharacter on a non-hex digit. Real /apps/ table names are
    // always valid 40-hex, but guard so parseKey's no-throw contract holds for arbitrary input.
    try
    {
        parsed.address = bcos::Address(std::string(addrHex), bcos::Address::FromHex);
    }
    catch (...)
    {
        return parsed;  // NotAnAccount: address is not valid hex
    }

    // A 32-byte binary row key is a storage slot, never a named field.
    if (row.size() == static_cast<size_t>(bcos::h256::SIZE))
    {
        parsed.kind = ParsedKey::Kind::StorageSlot;
        parsed.slot = bcos::h256(
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(row.data()), row.size()));
        return parsed;
    }

    if (row == ROW_NONCE)
    {
        parsed.kind = ParsedKey::Kind::Nonce;
    }
    else if (row == ROW_BALANCE)
    {
        parsed.kind = ParsedKey::Kind::Balance;
    }
    else if (row == ROW_CODE_HASH)
    {
        parsed.kind = ParsedKey::Kind::CodeHash;
    }
    else
    {
        // code/abi/alive/frozen/shard/status/... — a BCOS-specific extension row.
        parsed.kind = ParsedKey::Kind::BcosExtension;
    }
    return parsed;
}

/// Parse an Entry's stored value as a u256. The executor stores nonce and balance as DECIMAL
/// ASCII strings, NOT big-endian binary: balance is written with boost::lexical_cast<string> and
/// read with u256(string) (AccountPrecompiled.cpp), nonce with u256::convert_to<string>()
/// (TransactionExecutive/EVMAccount). An empty value decodes as 0 (the executor's value_or("0")
/// convention). Reading these bytes big-endian would corrupt the value (e.g. "100" -> 0x313030).
inline bcos::u256 entryToU256(bcos::storage::Entry const& entry)
{
    std::string_view const value = entry.get();
    if (value.empty())
    {
        return bcos::u256{0};
    }
    return bcos::u256{std::string{value}};
}

/// Copy an Entry's stored bytes into a 32-byte hash. Unlike nonce/balance, codeHash is stored as
/// the RAW 32-byte digest: TransactionExecutive writes codeHashEntry.importFields({codeHash
/// .asBytes()}), so the value bytes ARE the hash and are read directly.
inline bcos::h256 entryToH256(bcos::storage::Entry const& entry)
{
    std::string_view const value = entry.get();
    return bcos::h256(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(value.data()), value.size()));
}

}  // namespace detail

/// Group a flat key→Entry delta into per-account AccountDelta records.
///
/// @param delta    a range yielding (key, Entry) pairs — a storage2 range in
///                 production, a vector<pair<string, Entry>> in tests.
/// @param readView any object exposing `task::Task<bool> hasAccount(Address) const`
///                 (PR-09 supplies the real one; tests pass a mock). Used to set
///                 firstTouch = !hasAccount(addr).
/// @param l2Mode   true on an Ethereum-compatible (L2) chain: BCOS extension rows
///                 (abi/alive/frozen/shard/code/...) are an error and throw
///                 UnexpectedBCOSFieldInL2. false on a native BCOS chain: those
///                 rows are silently skipped (they don't affect the Ethereum
///                 4-tuple state root).
///
/// DEVIATION (vs plan): the plan threaded a `Features const&` and read
/// feature_mpt_state_root / feature_l2_ethereum_compat. Neither flag exists in
/// bcos-framework Features.h, so classify takes an explicit bool l2Mode instead;
/// the caller (PR-10) decides the value.
template <typename FlatDelta>
bcos::task::Task<MPTBuildInput> classify(FlatDelta const& delta, auto const& readView, bool l2Mode)
{
    MPTBuildInput result;

    for (auto&& [key, entry] : delta)
    {
        // string_view{key.data(), key.size()} (not string_view{key}) so this also accepts a
        // storage2 StateKey in PR-10: it exposes data()/size() but no operator string_view.
        detail::ParsedKey const parsed = detail::parseKey(std::string_view{key.data(), key.size()});
        using Kind = detail::ParsedKey::Kind;

        if (parsed.kind == Kind::NotAnAccount)
        {
            continue;
        }
        if (parsed.kind == Kind::BcosExtension)
        {
            if (l2Mode)
            {
                BOOST_THROW_EXCEPTION(
                    UnexpectedBCOSFieldInL2{} << bcos::errinfo_comment(
                        "classify: BCOS extension field present in L2 (Ethereum-compatible) "
                        "mode; key=" +
                        std::string{key}));
            }
            continue;  // native BCOS chain: not part of the Ethereum 4-tuple
        }

        bool const deleted = entry.status() == bcos::storage::Entry::DELETED;
        AccountDelta& account = result.perAccount[parsed.address];

        switch (parsed.kind)
        {
        case Kind::Nonce:
            if (deleted)
            {
                account.nonceState = AccountDelta::FieldState::Deleted;
            }
            else
            {
                account.nonceState = AccountDelta::FieldState::Updated;
                account.nonce = detail::entryToU256(entry);
            }
            break;
        case Kind::Balance:
            if (deleted)
            {
                account.balanceState = AccountDelta::FieldState::Deleted;
            }
            else
            {
                account.balanceState = AccountDelta::FieldState::Updated;
                account.balance = detail::entryToU256(entry);
            }
            break;
        case Kind::CodeHash:
            if (deleted)
            {
                account.codeHashState = AccountDelta::FieldState::Deleted;
            }
            else
            {
                account.codeHashState = AccountDelta::FieldState::Updated;
                account.codeHash = detail::entryToH256(entry);
            }
            break;
        case Kind::StorageSlot:
            if (deleted)
            {
                account.storageChanges[parsed.slot] = std::nullopt;
            }
            else
            {
                std::string_view const value = entry.get();
                account.storageChanges[parsed.slot] = bcos::bytes(value.begin(), value.end());
            }
            break;
        default:
            break;
        }
    }

    // Post-grouping passes: tombstone synthesis and first-touch probing.
    for (auto& [address, account] : result.perAccount)
    {
        if (account.nonceState == AccountDelta::FieldState::Deleted &&
            account.balanceState == AccountDelta::FieldState::Deleted &&
            account.codeHashState == AccountDelta::FieldState::Deleted)
        {
            account.tombstone = true;
        }
        account.firstTouch = !(co_await readView.hasAccount(address));
    }

    co_return result;
}

}  // namespace bcos::ledger::mpt
