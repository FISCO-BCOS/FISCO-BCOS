/*
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
 * @brief Physical-layout proof for MPT node rows as ORDINARY state rows: an Entry keyed
 *        pathNodeStateKey(position) written through a real RocksDBStorage2<StateKey, ...,
 *        StateKeyResolver, ...> lands under the literal "/mptp/s:<owner><compactPath>" key
 *        (constructed INDEPENDENTLY in this TU — PathKey.h exports the table names and the
 *        position codec, but no physical-key helper: StateKeyResolver is the sole authority
 *        for that), and those physical bytes decode back to the same StateKey via the
 *        resolver's split-at-first-colon reconstruction — the two facts the scheduler's
 *        view-riding node plane (ViewNodeStorage) and every raw-DB node reader depend on.
 * @file MPTNodeRowLayoutTest.cpp
 */

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <random>
#include <string>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::executor_v1;

namespace
{
// An OWNER deliberately RIDDLED with 0x3A (':') bytes: the layout's decode contract is that
// the first ':' of a node row's physical key always sits right after the table name (neither
// node table contains one), so colons inside the row key must not confuse the split.
h256 colonRiddledHash()
{
    h256 hash;
    for (size_t i = 0; i < h256::SIZE; ++i)
    {
        hash.data()[i] = (i % 2 == 0) ? byte{0x3A} : static_cast<byte>(0x11 + i);
    }
    return hash;
}

bytes sampleNodeRlp()
{
    // Arbitrary non-empty payload standing in for a node's RLP encoding.
    return bytes{0xC5, 0x84, 0xDE, 0xAD, 0xBE, 0xEF};
}

/// The node position this proof writes: two nibbles, so its compact path is one header byte
/// (even parity) plus one packed byte.
bcos::ledger::mpt::PathKey nodePosition(h256 const& owner)
{
    return {.scope = bcos::ledger::mpt::TrieScope::storage(owner), .position = bytes{0x09, 0x0c}};
}

// The literal physical key, built here by hand ON PURPOSE: the proof compares what the
// resolver-backed storage stack actually writes against bytes constructed with zero shared
// code (the production encode authority is StateKeyResolver alone).
std::string physicalNodeKey(h256 const& hash)
{
    std::string key = "/mptp/s:";
    key.append(reinterpret_cast<char const*>(hash.data()), h256::SIZE);
    key.push_back('\x00');  // compactPath header: even nibble count
    key.push_back('\x9c');  // the two nibbles, packed
    return key;
}
}  // namespace

struct TestMPTNodeKeyFixture
{
    std::string path = "./mptnodekeydb" + std::to_string(std::random_device{}());

    TestMPTNodeKeyFixture()
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;

        ::rocksdb::DB* db = nullptr;
        auto status = ::rocksdb::DB::Open(options, path, &db);
        BOOST_REQUIRE(status.ok());
        rocksDB.reset(db);
    }
    ~TestMPTNodeKeyFixture() { boost::filesystem::remove_all(path); }

    std::unique_ptr<::rocksdb::DB> rocksDB;
};


namespace
{
/// The physical bytes StateKeyResolver::encode emits for a StateKey — the single authority every
/// on-disk key goes through (RocksDBStorage2's write path).
std::string resolverPhysicalKey(StateKey const& stateKey)
{
    std::string out;
    StateKeyResolver::encode(stateKey, [&](bcos::bytesConstRef view) {
        out.append(reinterpret_cast<char const*>(view.data()), view.size());
    });
    return out;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(MPTNodeRowLayoutSuite, TestMPTNodeKeyFixture)

BOOST_AUTO_TEST_CASE(NodeRowPhysicalForm)
{
    // A storage node at position "a7c" of the trie owned by a recognisable 32-byte owner.
    bcos::h256 owner;
    for (unsigned i = 0; i < 32; ++i)
    {
        owner[i] = static_cast<byte>(i + 1);
    }
    bcos::ledger::mpt::PathKey const storageNode{
        .scope = bcos::ledger::mpt::TrieScope::storage(owner), .position = bytes{0x0a, 0x07, 0x0c}};

    std::string key = resolverPhysicalKey(bcos::ledger::mpt::pathNodeStateKey(storageNode));

    // "<table>" ':' "<owner 32B>" "<compactPath>" — the compact path of an odd-length position
    // is one header byte plus one packed byte.
    BOOST_CHECK_EQUAL(key.size(), bcos::ledger::mpt::kMPTStorageTable.size() + 1 + 32 + 2);
    BOOST_CHECK_EQUAL(key.substr(0, bcos::ledger::mpt::kMPTStorageTable.size() + 1), "/mptp/s:");
    for (unsigned i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(
            static_cast<uint8_t>(key[bcos::ledger::mpt::kMPTStorageTable.size() + 1 + i]),
            owner[i]);
    }

    // The account table shares the layout minus the owner, and puts its ':' at the same offset.
    bcos::ledger::mpt::PathKey const accountNode{
        .scope = bcos::ledger::mpt::TrieScope::account(), .position = bytes{0x0a, 0x07, 0x0c}};
    std::string accountKey = resolverPhysicalKey(bcos::ledger::mpt::pathNodeStateKey(accountNode));
    BOOST_CHECK_EQUAL(
        accountKey.substr(0, bcos::ledger::mpt::kMPTAccountTable.size() + 1), "/mptp/a:");
    BOOST_CHECK_EQUAL(accountKey.find(':'), key.find(':'));
    BOOST_CHECK_EQUAL(accountKey.size(), bcos::ledger::mpt::kMPTAccountTable.size() + 1 + 2);

    // The physical bytes ARE the StateKey's own flat buffer — encode adds nothing.
    auto stateKey = bcos::ledger::mpt::pathNodeStateKey(storageNode);
    BOOST_CHECK_EQUAL(std::string_view(stateKey.data(), stateKey.size()), key);
}

BOOST_AUTO_TEST_CASE(NodeRowResolverRoundTrip)
{
    // An owner containing a raw ':' (0x3a) — legal in a row key, and the case the
    // split-at-FIRST-colon rule exists for.
    bcos::h256 owner = h256::generateRandomFixedBytes();
    owner[0] = static_cast<byte>(':');
    bcos::ledger::mpt::PathKey const original{.scope = bcos::ledger::mpt::TrieScope::storage(owner),
        .position = bytes{0x09, 0x0c, 0x00, 0x0f}};
    std::string physical = resolverPhysicalKey(bcos::ledger::mpt::pathNodeStateKey(original));

    // decode is the inverse of encode, and the row key parses back into the same position.
    auto decoded = StateKeyResolver::decode(std::string_view(physical));
    BOOST_CHECK(decoded == bcos::ledger::mpt::pathNodeStateKey(original));
    StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, bcos::ledger::mpt::kMPTStorageTable);
    auto const parsed = bcos::ledger::mpt::parsePathNodeStateKey(decoded);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == original);
}


// The physical form is StateKey-NATIVE: a full-CF scan can hand the raw bytes to the
// resolver's single-string StateKey constructor and get the node table + the row key back,
// because the first ':' is always the table separator right after the table name.
BOOST_AUTO_TEST_CASE(physicalFormIsStateKeyNative)
{
    auto const hash = colonRiddledHash();
    auto const physicalKey = physicalNodeKey(hash);

    BOOST_CHECK_EQUAL(physicalKey.size(), bcos::ledger::mpt::kMPTStorageTable.size() + 1 + 32 + 2);
    BOOST_CHECK_EQUAL(physicalKey.find(':'), bcos::ledger::mpt::kMPTStorageTable.size());

    // The resolver's decode (single-string StateKey constructor, split at the first colon)
    // reconstructs the node table + the row key exactly — colons in the owner and all.
    auto const decoded = StateKeyResolver::decode(std::string_view(physicalKey));
    StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, bcos::ledger::mpt::kMPTStorageTable);
    BOOST_CHECK(decoded == bcos::ledger::mpt::pathNodeStateKey(nodePosition(hash)));
    auto const parsed = bcos::ledger::mpt::parsePathNodeStateKey(decoded);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(*parsed == nodePosition(hash));
}

// End-to-end physical-key proof over a real RocksDB, through the same code path commit
// uses: node rows and flat rows travel in ONE StateKey-keyed source (the shape of a
// block's mutable layer after the MPT build flushed into it), one RocksDBStorage2::merge,
// then a raw db Get with the literal 38-byte key.
BOOST_AUTO_TEST_CASE(mergeLandsUnderPhysicalKey)
{
    task::syncWait([this]() -> task::Task<void> {
        RocksDBStorage2<StateKey, StateValue, StateKeyResolver, StateValueResolver> storage(
            *rocksDB, StateKeyResolver{}, StateValueResolver{});

        auto const hash = colonRiddledHash();
        auto const nodeRlp = sampleNodeRlp();

        // One mutable-layer-shaped source carrying BOTH row kinds — no special node source.
        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            mutableLayer;
        storage::Entry flatEntry;
        flatEntry.set("flat-value");
        co_await storage2::writeOne(
            mutableLayer, StateKey{"/apps/test", "balance"}, std::move(flatEntry));
        storage::Entry nodeEntry;
        nodeEntry.set(bytes(nodeRlp));
        co_await storage2::writeOne(mutableLayer,
            bcos::ledger::mpt::pathNodeStateKey(nodePosition(hash)), std::move(nodeEntry));

        // One merge = one WriteBatch = one rocksdb Write (RocksDBStorage2::merge).
        co_await storage.merge(mutableLayer);

        // Raw Get with the literal physical key: proves the on-disk layout, not just the
        // resolver round-trip.
        std::string rawValue;
        auto status =
            rocksDB->Get(::rocksdb::ReadOptions(), physicalNodeKey(hash), std::addressof(rawValue));
        BOOST_REQUIRE(status.ok());

        // The value is Entry-encoded like every other value in the CF.
        auto decoded = storage::Entry::decode(
            bytesConstRef(reinterpret_cast<const byte*>(rawValue.data()), rawValue.size()));
        auto decodedView = decoded.get();
        bytes const decodedBytes(decodedView.begin(), decodedView.end());
        BOOST_CHECK(decodedBytes == nodeRlp);

        // The flat row landed too (same merge, same batch).
        auto flatBack = co_await storage2::readOne(storage, StateKey{"/apps/test", "balance"});
        BOOST_REQUIRE(flatBack);
        BOOST_CHECK_EQUAL(std::string(flatBack->get()), "flat-value");

        // And the ordinary StateKey read path resolves the node row.
        auto nodeBack = co_await storage2::readOne(
            storage, bcos::ledger::mpt::pathNodeStateKey(nodePosition(hash)));
        BOOST_REQUIRE(nodeBack);
        auto nodeBackView = nodeBack->get();
        bytes const nodeBackBytes(nodeBackView.begin(), nodeBackView.end());
        BOOST_CHECK(nodeBackBytes == nodeRlp);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
