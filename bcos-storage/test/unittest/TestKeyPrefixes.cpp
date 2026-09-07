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
 * @brief Unit tests for the path-addressed node-row key layout (KeyPrefixes.h's two tables +
 *        ledger::mpt::pathNodeStateKey), with StateKeyResolver as the ONLY physical
 *        encode/decode authority (KeyPrefixes.h deliberately exports no physical-key helpers
 *        of its own).
 * @file TestKeyPrefixes.cpp
 * @author: kyonRay
 * @date: 2026-05-12
 */
#include <bcos-ledger/mpt/PathKey.h>
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
using bcos::ledger::mpt::PathKey;
using bcos::ledger::mpt::pathNodeStateKey;
using bcos::ledger::mpt::TrieScope;

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
    // A storage node at position "a7c" of the trie owned by a recognisable 32-byte owner.
    h256 owner;
    for (unsigned i = 0; i < 32; ++i)
    {
        owner[i] = static_cast<byte>(i + 1);
    }
    PathKey const storageNode{
        .scope = TrieScope::storage(owner), .position = bytes{0x0a, 0x07, 0x0c}};

    std::string key = resolverPhysicalKey(pathNodeStateKey(storageNode));

    // "<table>" ':' "<owner 32B>" "<compactPath>" — the compact path of an odd-length position
    // is one header byte plus one packed byte.
    BOOST_CHECK_EQUAL(key.size(), kMPTStorageTable.size() + 1 + 32 + 2);
    BOOST_CHECK_EQUAL(key.substr(0, kMPTStorageTable.size() + 1), "/mptp/s:");
    for (unsigned i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<uint8_t>(key[kMPTStorageTable.size() + 1 + i]), owner[i]);
    }

    // The account table shares the layout minus the owner, and puts its ':' at the same offset.
    PathKey const accountNode{.scope = TrieScope::account(), .position = bytes{0x0a, 0x07, 0x0c}};
    std::string accountKey = resolverPhysicalKey(pathNodeStateKey(accountNode));
    BOOST_CHECK_EQUAL(accountKey.substr(0, kMPTAccountTable.size() + 1), "/mptp/a:");
    BOOST_CHECK_EQUAL(accountKey.find(':'), key.find(':'));
    BOOST_CHECK_EQUAL(accountKey.size(), kMPTAccountTable.size() + 1 + 2);

    // The physical bytes ARE the StateKey's own flat buffer — encode adds nothing.
    auto stateKey = pathNodeStateKey(storageNode);
    BOOST_CHECK_EQUAL(std::string_view(stateKey.data(), stateKey.size()), key);
}

BOOST_AUTO_TEST_CASE(NodeRowResolverRoundTrip)
{
    // An owner containing a raw ':' (0x3a) — legal in a row key, and the case the
    // split-at-FIRST-colon rule exists for.
    h256 owner = h256::generateRandomFixedBytes();
    owner[0] = static_cast<byte>(':');
    PathKey const original{
        .scope = TrieScope::storage(owner), .position = bytes{0x09, 0x0c, 0x00, 0x0f}};
    std::string physical = resolverPhysicalKey(pathNodeStateKey(original));

    // decode is the inverse of encode, and the row key parses back into the same position.
    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == pathNodeStateKey(original));
    executor_v1::StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, kMPTStorageTable);
    auto const parsed = bcos::ledger::mpt::parsePathNodeStateKey(decoded);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == original);
}

BOOST_AUTO_TEST_CASE(RetiredColonFreeLayoutIsNotAStateKey)
{
    // The RETIRED colon-free layout ("/mptp/a" + row key, no ':') cannot even be decoded as a
    // StateKey when the row key contains no 0x3A byte — the reason every table name is followed
    // by ':'. (A row key WITH a 0x3A would decode, but to a corrupted table/key split.)
    std::string legacy(37, '\0');
    legacy.replace(0, kMPTAccountTable.size(), kMPTAccountTable);
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

        // Demonstrate writing and reading a node row's physical key via rocksDB()
        std::string mptKey = resolverPhysicalKey(
            pathNodeStateKey(PathKey{.scope = TrieScope::storage(h256::generateRandomFixedBytes()),
                .position = bytes{0x01, 0x02}}));
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
