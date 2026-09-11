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
 * @file FlatToMPT.h
 * @brief Flat-KV → MPT value translation: the first-touch O(1) account-metadata read plus the
 *        Entry decoders for the executor's flat encodings (spec §5.3 path 2, Revision 2026-07-09)
 */
#pragma once

#include "Classify.h"
#include "Constants.h"
#include "Errors.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{

// This header serves scenario A — a legacy chain activating feature_mpt_state_root mid-flight
// (spec §5.10): only there does an account own flat-KV state that predates the MPT. Under the
// slot-level commitment model (Revision 2026-07-09) the only migration step left is reading the
// account's nonce/balance/codeHash once when a first-touch block leaves a field unwritten —
// storage slots are NEVER back-filled: a first-touch storage trie starts from emptyRootHash()
// and only ever holds slots written after activation (spec §4.2). The former bootstrap
// prefix-scan, preheat manifest and mpt-preheat CLI are deleted with the revision.

namespace detail
{

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
    // Construct from a std::string_view WITH its explicit length: boost::multiprecision's
    // number(std::basic_string_view) (number.hpp) parses exactly `value.size()` bytes via
    // assign_from_string_view, so it never falls back to the NUL-terminated const char*
    // overload and cannot read past the string_view into stale memory. (A plain u256{value}
    // would already select this overload — this makes the intent explicit and future-proof.)
    return bcos::u256{std::string_view{value.data(), value.size()}};
}

/// Copy an Entry's stored bytes into a 32-byte hash. Unlike nonce/balance, codeHash is stored as
/// the RAW 32-byte digest: TransactionExecutive writes the digest bytes verbatim, so the value
/// bytes ARE the hash and are read directly.
///
/// A length other than 32 throws rather than asserting: the h256 ctor would silently zero-pad or
/// truncate, producing a wrong account leaf and a wrong state root while the block commits
/// normally. An assert would vanish under NDEBUG — precisely in the release builds where that
/// silent fork would happen. Same reasoning as the zero-codeHash check in readFlatAccountMeta.
inline bcos::h256 entryToH256(bcos::storage::Entry const& entry)
{
    std::string_view const value = entry.get();
    if (value.size() != static_cast<size_t>(bcos::h256::SIZE))
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation{} << bcos::errinfo_comment(
                "codeHash row is not 32 raw bytes; size=" + std::to_string(value.size())));
    }
    return bcos::h256(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(value.data()), value.size()));
}

}  // namespace detail

/// An account's baseline nonce/balance/codeHash as stored in the flat KV, for first-touch fields
/// the block did not write (a first-touch account has no parent MPT leaf to merge onto).
struct FlatAccountMeta
{
    bcos::u256 nonce{};
    bcos::u256 balance{};
    bcos::h256 codeHash;  ///< emptyCodeHash() when the account has no codeHash row (EOA)
};

/// Read @p addr's nonce/balance/codeHash once from the flat state through @p flatView — the fork
/// view, so each read resolves against this block's delta layer first and falls through to the
/// parent flat state (spec §5.3 path 2: delta 层无该字段行的取 flat 值). Three O(1) named-row
/// reads; NEVER a slot scan (spec §4.2).
///
/// Missing rows take the Yellow Paper defaults: nonce/balance 0, codeHash = emptyCodeHash() —
/// the account leaf encodes codeHash verbatim, so a zero h256 here would produce a wrong leaf
/// hash. A codeHash row that is present but decodes to zero violates the executor contract
/// (codeHash = keccak(code), never zero) and throws rather than committing a forking leaf.
bcos::task::Task<FlatAccountMeta> readFlatAccountMeta(auto& flatView, bcos::Address const& addr)
{
    auto const table = accountTableName(addr);
    FlatAccountMeta meta;

    auto nonceEntry =
        co_await bcos::storage2::readOne(flatView, executor_v1::StateKeyView{table, ROW_NONCE});
    if (nonceEntry)
    {
        meta.nonce = detail::entryToU256(*nonceEntry);
    }
    auto balanceEntry =
        co_await bcos::storage2::readOne(flatView, executor_v1::StateKeyView{table, ROW_BALANCE});
    if (balanceEntry)
    {
        meta.balance = detail::entryToU256(*balanceEntry);
    }
    auto codeHashEntry =
        co_await bcos::storage2::readOne(flatView, executor_v1::StateKeyView{table, ROW_CODE_HASH});
    if (codeHashEntry)
    {
        meta.codeHash = detail::entryToH256(*codeHashEntry);
        if (meta.codeHash == bcos::h256{})
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "FlatToMPT: flat codeHash row decodes to a zero h256 for "
                                      "account " +
                                      addr.hex() + " (must be keccak(code) or absent)"));
        }
    }
    else
    {
        meta.codeHash = emptyCodeHash();
    }
    co_return meta;
}

}  // namespace bcos::ledger::mpt
