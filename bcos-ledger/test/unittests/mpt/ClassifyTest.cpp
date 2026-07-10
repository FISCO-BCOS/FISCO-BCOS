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
 * @brief Unit tests for the flat-state key parsers: parseAccountTable / classifyRowKey /
 *        extractTouchedAccounts (spec §5.2, Revision 2026-07-09b)
 */

#include "TestHelpers.h"
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt::test
{

namespace
{
// A fixed 40-hex account address and the matching 20-byte Address value.
constexpr std::string_view CLASSIFY_TEST_ADDR_HEX = "00112233445566778899aabbccddeeff00112233";
constexpr std::string_view CLASSIFY_TEST_TABLE = "/apps/00112233445566778899aabbccddeeff00112233";

bcos::Address classifyTestAddress()
{
    return bcos::Address(std::string{CLASSIFY_TEST_ADDR_HEX}, bcos::Address::FromHex);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ClassifySuite)

BOOST_AUTO_TEST_CASE(ParseAccountTableAcceptsAppsAddressOnly)
{
    auto parsed = parseAccountTable(CLASSIFY_TEST_TABLE);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == classifyTestAddress());

    BOOST_CHECK(!parseAccountTable("/sys/s_tables").has_value());
    BOOST_CHECK(!parseAccountTable("/tables/foo").has_value());
    BOOST_CHECK(!parseAccountTable("/apps/short").has_value());  // BCOS private short-name table
    // 40 chars but not hex — must not throw, just reject.
    BOOST_CHECK(!parseAccountTable("/apps/zz112233445566778899aabbccddeeff001122").has_value());
}

BOOST_AUTO_TEST_CASE(AccountTableNameRoundTrips)
{
    auto const addr = classifyTestAddress();
    auto const table = accountTableName(addr);
    BOOST_CHECK_EQUAL(table, std::string{CLASSIFY_TEST_TABLE});
    auto parsed = parseAccountTable(table);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == addr);
}

BOOST_AUTO_TEST_CASE(ClassifyRowKeyKinds)
{
    BOOST_CHECK(classifyRowKey(ROW_NONCE) == RowKind::Nonce);
    BOOST_CHECK(classifyRowKey(ROW_BALANCE) == RowKind::Balance);
    BOOST_CHECK(classifyRowKey(ROW_CODE_HASH) == RowKind::CodeHash);
    BOOST_CHECK(classifyRowKey(ROW_CODE) == RowKind::Code);

    // Any 32-byte binary row key is a storage slot — no field name is 32 bytes long.
    auto const slot = makeHash(0x05);
    std::string_view const slotRow{reinterpret_cast<char const*>(slot.data()), slot.size()};
    BOOST_CHECK(classifyRowKey(slotRow) == RowKind::StorageSlot);

    // Every other named row is a BCOS extension (skipped in scenario A, throws in scenario B).
    BOOST_CHECK(classifyRowKey("abi") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("alive") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("frozen") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("shard") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("status") == RowKind::BcosExtension);
    // 31 and 33 bytes are NOT slots.
    BOOST_CHECK(classifyRowKey(std::string(31, 'x')) == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey(std::string(33, 'x')) == RowKind::BcosExtension);
}

BOOST_AUTO_TEST_CASE(ExtractTouchedAccountsSortsAndDeduplicates)
{
    auto const addrA = makeAddress(0x0B);  // written second, must sort first? (0x0B > 0x0A)
    auto const addrB = makeAddress(0x0A);

    FlatMutableStorage delta;
    // Several rows for A (dedup), one row for B, plus rows extraction must skip.
    writeFlatRow(delta, accountFieldKey(addrA, ROW_NONCE), makeEntry("1"));
    writeFlatRow(delta, accountFieldKey(addrA, ROW_BALANCE), makeEntry("2"));
    writeFlatRow(delta, accountSlotKey(addrA, makeHash(0x01)), makeEntry("\x2a"));
    writeFlatRow(delta, accountFieldKey(addrB, ROW_NONCE), makeEntry("3"));
    writeFlatRow(delta, bcos::executor_v1::StateKey{"/sys/s_tables", "value"}, makeEntry("x"));
    writeFlatRow(delta, bcos::executor_v1::StateKey{"/apps/short_name", "nonce"}, makeEntry("y"));

    auto touched = bcos::task::syncWait(extractTouchedAccounts(delta));
    BOOST_REQUIRE_EQUAL(touched.size(), 2U);
    BOOST_CHECK(touched[0] == addrB);  // 0x0A... sorts before 0x0B...
    BOOST_CHECK(touched[1] == addrA);
}

BOOST_AUTO_TEST_CASE(ExtractTouchedAccountsSeesLogicallyDeletedRows)
{
    // A tombstone account's rows are deletions — logical deletion keeps the key in the delta
    // layer, and extraction must still report the account (key-only parsing).
    auto const addr = makeAddress(0x0C);
    FlatMutableStorage delta;
    bcos::task::syncWait(bcos::storage2::removeOne(delta, accountFieldKey(addr, ROW_NONCE)));

    auto touched = bcos::task::syncWait(extractTouchedAccounts(delta));
    BOOST_REQUIRE_EQUAL(touched.size(), 1U);
    BOOST_CHECK(touched.front() == addr);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
