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
 * @brief Unit tests for KeyPrefixes.h (MPT key namespace utilities)
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

BOOST_AUTO_TEST_SUITE(KeyPrefixesSuite)

BOOST_AUTO_TEST_CASE(MakeMPTNodeKeyFormat)
{
    h256 hash;
    // Fill with a recognisable pattern
    for (unsigned i = 0; i < 32; ++i)
    {
        hash[i] = static_cast<byte>(i + 1);
    }

    std::string key = makeMPTNodeKey(hash);

    // Must be exactly 37 bytes: 5 (prefix) + 32 (hash)
    BOOST_CHECK_EQUAL(key.size(), 37U);

    // Must start with "/mpt/"
    BOOST_CHECK_EQUAL(key.substr(0, 5), "/mpt/");

    // Last 32 bytes must match the raw hash bytes
    for (unsigned i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<uint8_t>(key[5 + i]), hash[i]);
    }
}

BOOST_AUTO_TEST_CASE(ParseMPTNodeKeyRoundTrip)
{
    h256 original = h256::generateRandomFixedBytes();
    std::string encoded = makeMPTNodeKey(original);
    auto decoded = parseMPTNodeKey(encoded);

    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(decoded.value(), original);
}

BOOST_AUTO_TEST_CASE(ParseMPTNodeKeyRejectsBadInputs)
{
    // Empty string
    BOOST_CHECK(!parseMPTNodeKey("").has_value());

    // Prefix only, no hash bytes
    BOOST_CHECK(!parseMPTNodeKey("/mpt/").has_value());

    // Prefix + truncated hash (less than 32 bytes)
    BOOST_CHECK(!parseMPTNodeKey("/mpt/short").has_value());

    // Wrong prefix: existing /apps/ namespace key
    BOOST_CHECK(!parseMPTNodeKey("/apps/abc:def").has_value());

    // Wrong prefix: existing s_ namespace key
    BOOST_CHECK(!parseMPTNodeKey("s_number_2_hash:123").has_value());

    // Exactly 37 bytes but wrong prefix: "/abc/" + 32 nul bytes (spec example)
    std::string almost(37, '\0');
    almost.replace(0, 5, "/abc/");
    BOOST_CHECK(!parseMPTNodeKey(almost).has_value());
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

        // Demonstrate writing and reading a /mpt/<hash> key via rocksDB()
        h256 hash = h256::generateRandomFixedBytes();
        std::string mptKey = makeMPTNodeKey(hash);
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

BOOST_AUTO_TEST_SUITE_END()
