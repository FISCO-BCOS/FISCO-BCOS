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
 *        mptNodeStateKey(hash) written through a real RocksDBStorage2<StateKey, ...,
 *        StateKeyResolver, ...> lands under the literal 38-byte "/mpt/:<digest>" key
 *        (makeMPTNodeKey), and those physical bytes decode back to the same StateKey via
 *        the resolver's split-at-first-colon reconstruction — the two facts the scheduler's
 *        view-riding node plane (ViewNodeStorage) and every raw-DB node reader depend on.
 * @file TestMPTNodeKey.cpp
 */

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-storage/KeyPrefixes.h>
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
// A digest deliberately RIDDLED with 0x3A (':') bytes: the layout's decode contract is that
// the first ':' of the 38-byte physical key always sits at index 5 (the "/mpt/" table has
// none), so colons inside the digest must not confuse the split.
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

BOOST_FIXTURE_TEST_SUITE(TestMPTNodeKey, TestMPTNodeKeyFixture)

// The physical form is StateKey-NATIVE: a full-CF scan can hand the raw 38 bytes to the
// resolver's single-string StateKey constructor and get table "/mpt/" + the raw digest
// back, because the first ':' is always the table separator at index 5.
BOOST_AUTO_TEST_CASE(physicalFormIsStateKeyNative)
{
    auto const hash = colonRiddledHash();
    auto const physicalKey = storage2::makeMPTNodeKey(hash);

    BOOST_CHECK_EQUAL(physicalKey.size(), storage2::kMPTKeyLength);  // 38
    BOOST_CHECK_EQUAL(physicalKey.find(':'), 5U);                    // table separator

    // Split-at-first-colon reconstruction (what StateKeyResolver::decode does).
    StateKey const decoded{std::string(physicalKey)};
    StateKeyView const view{decoded};
    BOOST_CHECK_EQUAL(view.m_table, storage2::kMPTTable);
    BOOST_CHECK_EQUAL(
        view.m_key, std::string_view(reinterpret_cast<char const*>(hash.data()), h256::SIZE));
    BOOST_CHECK(decoded == storage2::mptNodeStateKey(hash));

    // parseMPTNodeKey inverts the physical key.
    auto parsed = storage2::parseMPTNodeKey(physicalKey);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(*parsed, hash);
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
        co_await storage2::writeOne(
            mutableLayer, storage2::mptNodeStateKey(hash), std::move(nodeEntry));

        // One merge = one WriteBatch = one rocksdb Write (RocksDBStorage2::merge).
        co_await storage.merge(mutableLayer);

        // Raw Get with the literal 38-byte key: proves the on-disk layout, not just the
        // resolver round-trip.
        std::string rawValue;
        auto status = rocksDB->Get(
            ::rocksdb::ReadOptions(), storage2::makeMPTNodeKey(hash), std::addressof(rawValue));
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
        auto nodeBack = co_await storage2::readOne(storage, storage2::mptNodeStateKey(hash));
        BOOST_REQUIRE(nodeBack);
        auto nodeBackView = nodeBack->get();
        bytes const nodeBackBytes(nodeBackView.begin(), nodeBackView.end());
        BOOST_CHECK(nodeBackBytes == nodeRlp);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
