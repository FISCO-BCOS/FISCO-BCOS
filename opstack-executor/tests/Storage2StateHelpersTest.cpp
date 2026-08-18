// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the extracted Storage2State helpers (Storage2StateHelpers.h): account
// table-name derivation, slot/field classification, zero-slot and tombstone semantics.

#include <opstack-executor/Storage2StateHelpers.h>

#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::evm::evmstate
{
BOOST_AUTO_TEST_SUITE(Storage2StateHelpersTest)

BOOST_AUTO_TEST_CASE(accountTableNameShape)
{
    // Deterministic: /apps/ + hex_lower(20-byte address).
    evmc::address addr{};
    addr.bytes[0] = 0x00;
    addr.bytes[19] = 0xff;
    auto name = accountTableName(addr);
    BOOST_REQUIRE_EQUAL(name.size(), std::string("/apps/").size() + 40);
    BOOST_CHECK_EQUAL(name.substr(0, 6), "/apps/");
    // Trailing bytes are lowercase hex.
    BOOST_CHECK_EQUAL(name.substr(name.size() - 2), "ff");
    // All hex chars in the middle.
    for (size_t i = 6; i < name.size(); ++i)
        BOOST_CHECK(std::isxdigit(static_cast<unsigned char>(name[i])));
}

BOOST_AUTO_TEST_CASE(accountTableNameRoundTrip)
{
    // accountTableName → addressFromTableName is the identity on real addresses.
    evmc::address addr{};
    addr.bytes[0] = 0x12;
    addr.bytes[5] = 0xab;
    addr.bytes[19] = 0x99;
    auto name = accountTableName(addr);
    auto parsed = addressFromTableName(name);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(std::memcmp(parsed->bytes, addr.bytes, 20), 0);
}

BOOST_AUTO_TEST_CASE(addressFromTableNameRejectsNonAccount)
{
    // Non-account table paths → nullopt (no throw; the /apps/ namespace also holds
    // authorization / BFS link tables).
    BOOST_CHECK(!addressFromTableName("/apps/foo").has_value());
    BOOST_CHECK(!addressFromTableName("/sys/1000").has_value());
    BOOST_CHECK(!addressFromTableName("not-a-table").has_value());
    // Wrong-length hex → nullopt.
    BOOST_CHECK(!addressFromTableName("/apps/abc123").has_value());
}

BOOST_AUTO_TEST_CASE(isZeroSlotValueTest)
{
    // Exactly 32 zero bytes == absent (Ethereum trie semantics).
    std::string zeros(32, '\0');
    BOOST_CHECK(isZeroSlotValue(zeros));
    // Non-zero 32 bytes == present.
    std::string nonzero(32, '\x01');
    BOOST_CHECK(!isZeroSlotValue(nonzero));
    // Wrong length is conservatively "present".
    std::string shortZero(31, '\0');
    BOOST_CHECK(!isZeroSlotValue(shortZero));
    std::string longZero(33, '\0');
    BOOST_CHECK(!isZeroSlotValue(longZero));
}

BOOST_AUTO_TEST_CASE(isKnownAccountFieldTest)
{
    using Fields = bcos::ledger::ACCOUNT_TABLE_FIELDS;
    BOOST_CHECK(isKnownAccountField(Fields::BALANCE));
    BOOST_CHECK(isKnownAccountField(Fields::CODE_HASH));
    BOOST_CHECK(isKnownAccountField(Fields::NONCE));
    // A 32-byte storage-slot key is NOT a field name.
    BOOST_CHECK(!isKnownAccountField(std::string(32, 'x')));
    // A field name is recognized by its exact spelling (ACCOUNT_TABLE_FIELDS uses lowercase).
    BOOST_CHECK(isKnownAccountField("balance"));
    // An unrelated short string is not a field name.
    BOOST_CHECK(!isKnownAccountField("foo"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::evmstate
