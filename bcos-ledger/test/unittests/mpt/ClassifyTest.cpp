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
 *        (spec §5.2, Revision 2026-07-09b)
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

/// "/apps/" + the 20 raw address bytes — the feature_raw_address table layout.
std::string binaryTestTable(bcos::Address const& addr)
{
    std::string table{APPS_TABLE_PREFIX};
    table.append(reinterpret_cast<char const*>(addr.data()), addr.size());
    return table;
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

BOOST_AUTO_TEST_CASE(ParseAccountTableAcceptsRawAddressTable)
{
    // The feature_raw_address layout: "/apps/" + the 20 raw address bytes, taken verbatim.
    auto const addr = classifyTestAddress();
    auto parsed = parseAccountTable(binaryTestTable(addr));
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == addr);

    // Bytes that are NOT ASCII hex digits are still address bytes — no hex decoding applies.
    bcos::Address nonAscii{};
    nonAscii.data()[0] = static_cast<bcos::byte>(0xFF);
    nonAscii.data()[19] = static_cast<bcos::byte>(0xFE);
    auto parsedNonAscii = parseAccountTable(binaryTestTable(nonAscii));
    BOOST_REQUIRE(parsedNonAscii.has_value());
    BOOST_CHECK(*parsedNonAscii == nonAscii);

    // A suffix that is 20 hex-LOOKING chars is raw bytes, not a hex decode of 10 bytes.
    std::string const twentyHexChars{APPS_TABLE_PREFIX};
    auto const twentyAsBytes = twentyHexChars + std::string("01234567890123456789");
    auto parsedRaw = parseAccountTable(twentyAsBytes);
    BOOST_REQUIRE(parsedRaw.has_value());
    BOOST_CHECK_EQUAL(parsedRaw->data()[0], static_cast<bcos::byte>('0'));
    BOOST_CHECK_EQUAL(parsedRaw->data()[19], static_cast<bcos::byte>('9'));

    // Lengths other than 20 and 40 reject; a 21st raw byte (a row key bleeding into the table
    // view) is not an account table.
    BOOST_CHECK(!parseAccountTable(std::string(APPS_TABLE_PREFIX) + std::string(19, 'a'))
                    .has_value());
    BOOST_CHECK(!parseAccountTable(std::string(APPS_TABLE_PREFIX) + std::string(21, 'a'))
                    .has_value());
    BOOST_CHECK(!parseAccountTable(std::string(APPS_TABLE_PREFIX) + std::string(39, 'a'))
                    .has_value());
    BOOST_CHECK(!parseAccountTable(std::string(APPS_TABLE_PREFIX) + std::string(41, 'a'))
                    .has_value());
    // The bare prefix and a prefix-only lookalike reject.
    BOOST_CHECK(!parseAccountTable(APPS_TABLE_PREFIX).has_value());
    BOOST_CHECK(!parseAccountTable("/apps").has_value());
}

BOOST_AUTO_TEST_CASE(AccountTableNameRoundTrips)
{
    auto const addr = classifyTestAddress();
    auto const table = accountTableName(addr);
    BOOST_CHECK_EQUAL(table, std::string{CLASSIFY_TEST_TABLE});
    auto parsed = parseAccountTable(table);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == addr);

    // The raw-address layout round-trips through the parser as well.
    auto const binaryTable = binaryTestTable(addr);
    BOOST_CHECK_EQUAL(binaryTable.size(), APPS_TABLE_PREFIX.size() + 20);
    auto parsedBinary = parseAccountTable(binaryTable);
    BOOST_REQUIRE(parsedBinary.has_value());
    BOOST_CHECK(*parsedBinary == addr);
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

    // A KNOWN non-Ethereum field: skipped in scenario A, throws in scenario B.
    BOOST_CHECK(classifyRowKey("abi") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("alive") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("frozen") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("shard") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("status") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("last_update") == RowKind::BcosExtension);
    BOOST_CHECK(classifyRowKey("last_status") == RowKind::BcosExtension);

    // Anything else has never been classified — it throws in BOTH modes rather than falling out
    // of the state commitment silently. 31 and 33 bytes are NOT slots, so they land here too.
    BOOST_CHECK(classifyRowKey("someFutureField") == RowKind::UnknownField);
    BOOST_CHECK(classifyRowKey(std::string(31, 'x')) == RowKind::UnknownField);
    BOOST_CHECK(classifyRowKey(std::string(33, 'x')) == RowKind::UnknownField);
    // Near-misses of real field names must not be whitelisted by accident.
    BOOST_CHECK(classifyRowKey("Abi") == RowKind::UnknownField);
    BOOST_CHECK(classifyRowKey("abi ") == RowKind::UnknownField);
    BOOST_CHECK(classifyRowKey("") == RowKind::UnknownField);
}

BOOST_AUTO_TEST_CASE(KnownExtensionWhitelistMirrorsTheExecutorConstants)
{
    // KNOWN_BCOS_EXTENSION_FIELDS mirrors bcos-executor/src/Common.h:81-98. Copied here rather
    // than included so bcos-ledger keeps no build dependency on bcos-executor; this test is what
    // catches the two drifting apart.
    BOOST_CHECK_EQUAL(KNOWN_BCOS_EXTENSION_FIELDS.size(), 7U);
    for (auto const& field : KNOWN_BCOS_EXTENSION_FIELDS)
    {
        BOOST_CHECK(isKnownBcosExtensionField(field));
        BOOST_CHECK(classifyRowKey(field) == RowKind::BcosExtension);
    }
    // Core fields must never appear in the extension whitelist.
    BOOST_CHECK(!isKnownBcosExtensionField(ROW_NONCE));
    BOOST_CHECK(!isKnownBcosExtensionField(ROW_BALANCE));
    BOOST_CHECK(!isKnownBcosExtensionField(ROW_CODE_HASH));
    BOOST_CHECK(!isKnownBcosExtensionField(ROW_CODE));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
