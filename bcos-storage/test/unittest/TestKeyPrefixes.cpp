/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unit tests for KeyPrefixes.h: the "/mpt/" node-row key layout, with
 *        StateKeyResolver as the ONLY physical encode/decode authority (KeyPrefixes.h
 *        deliberately exports no physical-key helpers of its own).
 * @file TestKeyPrefixes.cpp
 * @author: kyonRay
 * @date: 2026-05-12
 */
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-utilities/FixedBytes.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <random>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::rocksdb;

namespace
{
/// The physical bytes StateKeyResolver::encode emits for a StateKey — the single authority
/// every on-disk key goes through (RocksDBStorage2's write path).
std::string resolverPhysicalKey(executor_v1::StateKey const& stateKey)
{
    std::string out;
    StateKeyResolver::encode(stateKey, [&](bcos::bytesConstRef view) {
        out.append(reinterpret_cast<char const*>(view.data()), view.size());
    });
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(KeyPrefixesSuite)

BOOST_AUTO_TEST_CASE(NodeRowPhysicalForm)
{
    h256 hash;
    // Fill with a recognisable pattern
    for (unsigned i = 0; i < 32; ++i)
    {
        hash[i] = static_cast<byte>(i + 1);
    }

    std::string key = resolverPhysicalKey(mptNodeStateKey(hash));

    // Must be exactly 38 bytes: 5 (table) + 1 (':') + 32 (hash)
    BOOST_CHECK_EQUAL(key.size(), kMPTKeyLength);
    BOOST_CHECK_EQUAL(key.size(), 38U);

    // Must start with the "/mpt/:" StateKey serialization prefix
    BOOST_CHECK_EQUAL(key.substr(0, 6), "/mpt/:");

    // Last 32 bytes must match the raw hash bytes
    for (unsigned i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<uint8_t>(key[6 + i]), hash[i]);
    }

    // The physical bytes ARE the StateKey's own flat buffer — encode adds nothing.
    auto stateKey = mptNodeStateKey(hash);
    BOOST_CHECK_EQUAL(std::string_view(stateKey.data(), stateKey.size()), key);
}

BOOST_AUTO_TEST_CASE(NodeRowResolverRoundTrip)
{
    h256 original = h256::generateRandomFixedBytes();
    std::string physical = resolverPhysicalKey(mptNodeStateKey(original));

    // decode is the inverse of encode: same StateKey, table "/mpt/", key = the raw digest.
    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == mptNodeStateKey(original));
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, kMPTTable);
    BOOST_CHECK_EQUAL(
        view.m_key, std::string_view(reinterpret_cast<char const*>(original.data()), h256::SIZE));
}

BOOST_AUTO_TEST_CASE(RawAddressTableWithColonByteRoundTrip)
{
    // Binary-layout account tables are "/s/" + 20 raw address bytes, and the
    // address itself can contain 0x3A (':'). decode must split at the fixed offset
    // ("/s/".size() + 20 == 23), not at the first ':' inside the address.
    std::string address(20, 'a');
    address[0] = ':';   // first-':' would land here without the fixed-offset rule
    address[7] = ':';   // ':' strictly inside the address
    address[19] = ':';  // ':' as the last address byte, right before the real separator

    executor_v1::StateKey const stateKey("/s/" + address, "nonce");
    std::string const physical = resolverPhysicalKey(stateKey);

    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == stateKey);
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, "/s/" + address);
    BOOST_CHECK_EQUAL(view.m_key, "nonce");
    BOOST_CHECK_EQUAL(decoded.m_split, 23U);
}

BOOST_AUTO_TEST_CASE(HexAddressTableKeepsFirstColonSplit)
{
    // The legacy 40-hex form lives under "/apps/", which has NO fixed-offset rule at all:
    // "/apps/" tables always split at the first ':' — the binary layout moved to the
    // reserved "/s/" namespace, so nothing under "/apps/" can place a ':' inside a table
    // name by design. Decodes exactly as before.
    std::string const table = "/apps/" + std::string(40, 'b');
    executor_v1::StateKey const stateKey(table, "nonce");
    std::string const physical = resolverPhysicalKey(stateKey);

    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == stateKey);
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, table);
    BOOST_CHECK_EQUAL(view.m_key, "nonce");
    BOOST_CHECK_EQUAL(decoded.m_split, 46U);
}

BOOST_AUTO_TEST_CASE(RetiredColonFreeLayoutIsNotAStateKey)
{
    // The RETIRED 37-byte layout ("/mpt/" + raw digest, no ':') cannot even be decoded as a
    // StateKey when the digest contains no 0x3A byte — the reason the layout moved to
    // "/mpt/:". (A digest WITH a 0x3A would decode, but to a corrupted table/key split.)
    std::string legacy(37, '\0');
    legacy.replace(0, 5, "/mpt/");
    BOOST_CHECK_THROW(
        StateKeyResolver::decode(std::string_view(legacy)), executor_v1::NoTableSpliterError);
}

BOOST_AUTO_TEST_CASE(RocksDBAccessorExposed)
{
    std::string path = "./mptkey_test_" + std::to_string(std::random_device{}());
    ::rocksdb::DB* rawPtr = nullptr;

    {
        ::rocksdb::Options options;
        options.create_if_missing = true;
        ::rocksdb::Status s = ::rocksdb::DB::Open(options, path, &rawPtr);
        BOOST_REQUIRE(s.ok());
        BOOST_REQUIRE(rawPtr != nullptr);
    }

    // Inner scope: keep dbOwner + storage lifetime strictly shorter than the on-disk path.
    // Background RocksDB threads (compaction, flush) may still touch lock/SST files while
    // the DB is open, so the directory must outlive both objects' destructors — otherwise
    // remove_all races the close path and can spuriously fail on lock-tracking filesystems.
    {
        std::unique_ptr<::rocksdb::DB> dbOwner(rawPtr);

        RocksDBStorage2<executor_v1::StateKey, storage::Entry, StateKeyResolver, StateValueResolver>
            storage(*dbOwner, StateKeyResolver{}, StateValueResolver{});

        // rocksDB() must reference the same underlying DB instance
        BOOST_CHECK_EQUAL(&storage.rocksDB(), rawPtr);

        // Demonstrate writing and reading a "/mpt/:<hash>" key via rocksDB()
        h256 hash = h256::generateRandomFixedBytes();
        std::string mptKey = resolverPhysicalKey(mptNodeStateKey(hash));
        std::string mptValue = "mpt_node_data";

        ::rocksdb::WriteOptions wo;
        auto putStatus =
            storage.rocksDB().Put(wo, storage.rocksDB().DefaultColumnFamily(), mptKey, mptValue);
        BOOST_REQUIRE(putStatus.ok());

        std::string readback;
        ::rocksdb::ReadOptions ro;
        auto getStatus =
            storage.rocksDB().Get(ro, storage.rocksDB().DefaultColumnFamily(), mptKey, &readback);
        BOOST_REQUIRE(getStatus.ok());
        BOOST_CHECK_EQUAL(readback, mptValue);
    }  // storage destructor, then dbOwner destructor (LIFO) — DB fully closed here

    boost::filesystem::remove_all(path);
}

BOOST_AUTO_TEST_CASE(ShortAppsTableWithColonAnywhereIsNotAmbiguous)
{
    // The OLD known ambiguity is gone: when the binary layout lived under "/apps/", a short
    // "/apps/" table whose key placed a ':' at the fixed binary split offset was misread as
    // a binary-address table (the retired ShortAppsTableWithColonAtBinaryOffsetIsAmbiguous
    // pin). With the binary layout under "/s/", "/apps/" tables split at the FIRST ':' —
    // unconditionally, whatever the table/key content — so this exact former trap now
    // round-trips cleanly.
    std::string const table = "/apps/foo";              // 9 chars, shorter than any account name
    std::string const key = std::string(16, 'x') + ":bar";  // ':' at flat offset 9+1+16 == 26

    executor_v1::StateKey const stateKey(table, key);
    std::string const physical = resolverPhysicalKey(stateKey);
    BOOST_CHECK_EQUAL(physical, table + ":" + key);

    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == stateKey);
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, table);
    BOOST_CHECK_EQUAL(view.m_key, key);
    BOOST_CHECK_EQUAL(decoded.m_split, 9U);
}

BOOST_AUTO_TEST_CASE(WrongLengthBinaryPrefixTableFallsBackToFirstColon)
{
    // "/s/" is reserved for the 20-byte binary account tables, but splitPosition must still
    // behave sanely on arbitrary input: a "/s/" name that is NOT exactly 23 bytes gets no
    // fixed-offset rule and falls back to the first ':' split. 19 bytes here; a ':' inside
    // the name is then the separator (garbage in, plain first-':' semantics out).
    std::string const table = "/s/" + std::string(19, 'a');  // 22 chars — not a binary table
    executor_v1::StateKey const stateKey(table, "nonce");
    std::string const physical = resolverPhysicalKey(stateKey);

    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == stateKey);
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, table);
    BOOST_CHECK_EQUAL(view.m_key, "nonce");
    BOOST_CHECK_EQUAL(decoded.m_split, 22U);
}

BOOST_AUTO_TEST_SUITE_END()
