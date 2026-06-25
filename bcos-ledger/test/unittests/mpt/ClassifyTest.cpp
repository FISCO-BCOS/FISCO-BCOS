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
 * @file ClassifyTest.cpp
 * @brief Unit tests for classify(): flat key→Entry delta into per-account
 *        AccountDelta with first-touch / subsequent / tombstone classification
 *        (spec §5.2)
 */

#include "TestHelpers.h"
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

namespace
{
// The flat-state delta classify() consumes: a range of (key, Entry) pairs.
using ClassifyFlatDelta = std::vector<std::pair<std::string, bcos::storage::Entry>>;

// A fixed 40-hex account address and the matching 20-byte Address value.
constexpr std::string_view CLASSIFY_TEST_ADDR_HEX = "00112233445566778899aabbccddeeff00112233";
constexpr std::string_view CLASSIFY_TEST_TABLE = "/apps/00112233445566778899aabbccddeeff00112233";

bcos::Address classifyTestAddress()
{
    return bcos::Address(std::string{CLASSIFY_TEST_ADDR_HEX}, bcos::Address::FromHex);
}

// Build "<table>:<row>" for a named field row.
std::string classifyFieldKey(std::string_view row)
{
    std::string key{CLASSIFY_TEST_TABLE};
    key.push_back(':');
    key.append(row);
    return key;
}

// Build "<table>:<32-byte-binary-slot>" for a storage slot.
std::string classifySlotKey(bcos::h256 const& slot)
{
    std::string key{CLASSIFY_TEST_TABLE};
    key.push_back(':');
    key.append(reinterpret_cast<char const*>(slot.data()), slot.size());
    return key;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ClassifySuite)

// ---------------------------------------------------------------------------
// Test 1: a single account with nonce + balance writes groups under one address,
// both fields Updated, firstTouch true (mock hasAccount returns false).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(GroupsByAddressFirstTouch)
{
    ClassifyFlatDelta delta;
    // nonce/balance are stored as decimal ASCII strings by the executor, not big-endian binary.
    delta.emplace_back(classifyFieldKey(ROW_NONCE), makeEntry("1"));
    delta.emplace_back(classifyFieldKey(ROW_BALANCE), makeEntry("100"));

    MockReadView view;  // address unset → hasAccount false → firstTouch true

    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const it = result.perAccount.find(classifyTestAddress());
    BOOST_REQUIRE(it != result.perAccount.end());
    auto const& account = it->second;

    BOOST_CHECK(account.firstTouch);
    BOOST_CHECK(account.nonceState == AccountDelta::FieldState::Updated);
    BOOST_CHECK(account.balanceState == AccountDelta::FieldState::Updated);
    BOOST_CHECK_EQUAL(account.nonce, 1);
    BOOST_CHECK_EQUAL(account.balance, 100);
    BOOST_CHECK(account.codeHashState == AccountDelta::FieldState::Unchanged);
    BOOST_CHECK(!account.tombstone);
}

// ---------------------------------------------------------------------------
// Test 2: a storage slot write lands in storageChanges keyed by the 32-byte slot.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(StorageSlotEntersStorageChanges)
{
    bcos::h256 const slot = makeHash(0x05);
    ClassifyFlatDelta delta;
    delta.emplace_back(classifySlotKey(slot), makeEntry("\xde\xad"));

    MockReadView view;
    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const& account = result.perAccount.begin()->second;
    BOOST_REQUIRE_EQUAL(account.storageChanges.size(), 1U);
    auto const slotIt = account.storageChanges.find(slot);
    BOOST_REQUIRE(slotIt != account.storageChanges.end());
    BOOST_REQUIRE(slotIt->second.has_value());
    BOOST_CHECK_EQUAL(slotIt->second->size(), 2U);
}

// ---------------------------------------------------------------------------
// Test 2b: a codeHash write is read back as the raw 32-byte digest (the executor stores it via
// codeHash.asBytes(), not decimal ASCII), with state Updated.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(CodeHashUpdatedReadsRawBytes)
{
    bcos::h256 const expected = makeHash(0x42);
    ClassifyFlatDelta delta;
    delta.emplace_back(classifyFieldKey(ROW_CODE_HASH),
        makeEntry(
            std::string_view(reinterpret_cast<char const*>(expected.data()), expected.size())));

    MockReadView view;
    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const& account = result.perAccount.begin()->second;
    BOOST_CHECK(account.codeHashState == AccountDelta::FieldState::Updated);
    BOOST_CHECK_EQUAL(account.codeHash, expected);
    BOOST_CHECK(account.nonceState == AccountDelta::FieldState::Unchanged);
}

// ---------------------------------------------------------------------------
// Test 3 (Scenario A, l2Mode=false): BCOS extension rows are silently skipped;
// nonce is kept, storageChanges stays empty.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(BcosExtensionSkippedWhenNotL2)
{
    ClassifyFlatDelta delta;
    delta.emplace_back(classifyFieldKey(ROW_NONCE), makeEntry("1"));
    delta.emplace_back(classifyFieldKey("abi"), makeEntry("\xde\xad\xbe\xef"));
    delta.emplace_back(classifyFieldKey("alive"), makeEntry("\x01"));
    delta.emplace_back(classifyFieldKey("frozen"), makeEntry("\x00"));
    delta.emplace_back(classifyFieldKey("shard"), makeEntry("g1"));
    delta.emplace_back(classifyFieldKey("code"), makeEntry("\x60\x60"));

    MockReadView view;
    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const& account = result.perAccount.begin()->second;
    BOOST_CHECK(account.nonceState == AccountDelta::FieldState::Updated);
    BOOST_CHECK(account.balanceState == AccountDelta::FieldState::Unchanged);
    BOOST_CHECK(account.codeHashState == AccountDelta::FieldState::Unchanged);
    BOOST_CHECK(account.storageChanges.empty());
    BOOST_CHECK(!account.tombstone);
}

// ---------------------------------------------------------------------------
// Test 4 (Scenario B, l2Mode=true): a BCOS extension row throws.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(BcosExtensionThrowsWhenL2)
{
    ClassifyFlatDelta delta;
    delta.emplace_back(classifyFieldKey("abi"), makeEntry("\xde\xad\xbe\xef"));

    MockReadView view;
    BOOST_CHECK_THROW(
        bcos::task::syncWait(classify(delta, view, /*l2Mode=*/true)), UnexpectedBCOSFieldInL2);
}

// ---------------------------------------------------------------------------
// Test 5: SELFDESTRUCT → tombstone. All three core fields DELETED, account
// already exists (mock hasAccount returns true → firstTouch false).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(SelfdestructTombstone)
{
    ClassifyFlatDelta delta;
    delta.emplace_back(classifyFieldKey(ROW_NONCE), makeDeletedEntry());
    delta.emplace_back(classifyFieldKey(ROW_BALANCE), makeDeletedEntry());
    delta.emplace_back(classifyFieldKey(ROW_CODE_HASH), makeDeletedEntry());

    MockReadView view;
    view.setHasAccount(classifyTestAddress(), true);  // tombstone needs a pre-existing account

    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const& account = result.perAccount.begin()->second;
    BOOST_CHECK(account.tombstone);
    BOOST_CHECK(!account.firstTouch);
    BOOST_CHECK(account.nonceState == AccountDelta::FieldState::Deleted);
    BOOST_CHECK(account.balanceState == AccountDelta::FieldState::Deleted);
    BOOST_CHECK(account.codeHashState == AccountDelta::FieldState::Deleted);
}

// ---------------------------------------------------------------------------
// Test 6: rows under a non-/apps/ table (and keys with no separator) are ignored.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(NonAccountRowsIgnored)
{
    ClassifyFlatDelta delta;
    delta.emplace_back("/sys/s_tables:value", makeEntry("\x01"));
    delta.emplace_back("/tables/foo:nonce", makeEntry("\x01"));
    delta.emplace_back("no_colon_key", makeEntry("\x01"));
    delta.emplace_back(classifyFieldKey(ROW_NONCE), makeEntry("7"));  // the only real account row

    MockReadView view;
    auto const result = bcos::task::syncWait(classify(delta, view, /*l2Mode=*/false));

    BOOST_REQUIRE_EQUAL(result.perAccount.size(), 1U);
    auto const& account = result.perAccount.begin()->second;
    BOOST_CHECK(account.nonceState == AccountDelta::FieldState::Updated);
    BOOST_CHECK_EQUAL(account.nonce, 7);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
