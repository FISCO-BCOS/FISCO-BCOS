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
 * @brief MPTNodeReadStorage — the eth_getProof node reader adapter (M8.3 wiring): node rows
 *        written through the ORDINARY state write path (Entry keyed mptNodeStateKey(hash)
 *        into a real RocksDBStorage2<StateKey, ..., StateKeyResolver, ...>) read back by raw
 *        h256 through the adapter, both directly and through the type-erased
 *        AnyStorage<h256, bytes> handle makeMPTNodeReader() hands to NodeService.
 * @file TestMPTNodeReadStorage.cpp
 */

#include "bcos-framework/storage2/AnyStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::executor_v1;

namespace
{
using StateRocksDB = RocksDBStorage2<StateKey, StateValue, StateKeyResolver, StateValueResolver>;

bytes nodeRlpA()
{
    return bytes{0xC5, 0x84, 0xDE, 0xAD, 0xBE, 0xEF};
}
bytes nodeRlpB()
{
    return bytes{0xC3, 0x82, 0x13, 0x37};
}

/// Write a node row exactly the way production does: an Entry under mptNodeStateKey(hash)
/// through the ordinary storage2 write path — no adapter involved on the write side.
task::Task<void> writeNodeRow(auto& storage, h256 const& hash, bytes rlp)
{
    storage::Entry entry;
    entry.set(std::move(rlp));
    co_await storage2::writeOne(storage, storage2::mptNodeStateKey(hash), std::move(entry));
}
}  // namespace

struct TestMPTNodeReadStorageFixture
{
    std::string path = "./mptnodereaderdb" + std::to_string(std::random_device{}());

    TestMPTNodeReadStorageFixture()
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;

        ::rocksdb::DB* db = nullptr;
        auto status = ::rocksdb::DB::Open(options, path, &db);
        BOOST_REQUIRE(status.ok());
        rocksDB.reset(db);
    }
    ~TestMPTNodeReadStorageFixture() { boost::filesystem::remove_all(path); }

    std::unique_ptr<::rocksdb::DB> rocksDB;
    h256 hashA{1U};
    h256 hashB{2U};
    h256 hashMissing{3U};
};

BOOST_FIXTURE_TEST_SUITE(TestMPTNodeReadStorage, TestMPTNodeReadStorageFixture)

// Ordinary state write path in, adapter read by raw h256 out; a hash never written reads
// back as nullopt — the exact miss shape generateProof's proofWalk keys its rootMissing /
// broken-chain handling on.
BOOST_AUTO_TEST_CASE(readOneRoundTripAndMiss)
{
    task::syncWait([this]() -> task::Task<void> {
        StateRocksDB storage(*rocksDB, StateKeyResolver{}, StateValueResolver{});
        co_await writeNodeRow(storage, hashA, nodeRlpA());
        co_await writeNodeRow(storage, hashB, nodeRlpB());

        storage2::MPTNodeReadStorage reader(storage);
        auto valueA = co_await reader.readOne(hashA);
        BOOST_REQUIRE(valueA.has_value());
        BOOST_CHECK(*valueA == nodeRlpA());

        // Negative control: a different hash yields DIFFERENT bytes (the adapter keys reads
        // by hash, it does not replay a constant), and an absent hash yields nullopt.
        auto valueB = co_await reader.readOne(hashB);
        BOOST_REQUIRE(valueB.has_value());
        BOOST_CHECK(*valueB != *valueA);
        BOOST_CHECK(*valueB == nodeRlpB());
        auto miss = co_await reader.readOne(hashMissing);
        BOOST_CHECK(!miss.has_value());
    }());
}

// readSome preserves request order and represents per-key misses as gaps, matching the
// batched-read contract the storage2 free functions dispatch to.
BOOST_AUTO_TEST_CASE(readSomeKeepsOrderAndGaps)
{
    task::syncWait([this]() -> task::Task<void> {
        StateRocksDB storage(*rocksDB, StateKeyResolver{}, StateValueResolver{});
        co_await writeNodeRow(storage, hashA, nodeRlpA());
        co_await writeNodeRow(storage, hashB, nodeRlpB());

        storage2::MPTNodeReadStorage reader(storage);
        std::vector<h256> keys{hashA, hashMissing, hashB};
        auto values = co_await storage2::readSome(reader, keys);
        BOOST_REQUIRE_EQUAL(values.size(), 3U);
        BOOST_REQUIRE(values[0].has_value());
        BOOST_CHECK(*values[0] == nodeRlpA());
        BOOST_CHECK(!values[1].has_value());  // negative control: the gap stays a gap
        BOOST_REQUIRE(values[2].has_value());
        BOOST_CHECK(*values[2] == nodeRlpB());
    }());
}

// The production shape: makeMPTNodeReader() returns an AnyStorage<h256, bytes> handle that
// OWNS its adapter (aliasing shared_ptr), so the handle stays valid with no other reference
// to the adapter — only the backend row storage must outlive it.
BOOST_AUTO_TEST_CASE(anyStorageHandleOwnsItsAdapter)
{
    task::syncWait([this]() -> task::Task<void> {
        StateRocksDB storage(*rocksDB, StateKeyResolver{}, StateValueResolver{});
        co_await writeNodeRow(storage, hashA, nodeRlpA());

        auto reader = storage2::makeMPTNodeReader(storage);
        BOOST_REQUIRE(reader != nullptr);
        auto value = co_await reader->readOne(hashA);
        BOOST_REQUIRE(value.has_value());
        BOOST_CHECK(*value == nodeRlpA());
        auto miss = co_await reader->readOne(hashMissing);
        BOOST_CHECK(!miss.has_value());  // negative control through the erased handle too
    }());
}

// The adapter is a read-only view: every mutating entry point AnyStorage's type-erasure
// forces onto it throws instead of touching the backend; reads on the same handle keep
// working afterwards (negative control that the throw is per-operation, not poisoning).
BOOST_AUTO_TEST_CASE(mutationsThrowReadsSurvive)
{
    StateRocksDB storage(*rocksDB, StateKeyResolver{}, StateValueResolver{});
    task::syncWait(writeNodeRow(storage, hashA, nodeRlpA()));

    auto reader = storage2::makeMPTNodeReader(storage);
    BOOST_CHECK_THROW(task::syncWait(reader->writeOne(hashB, nodeRlpB())), std::logic_error);
    BOOST_CHECK_THROW(task::syncWait(reader->removeOne(hashA)), std::logic_error);
    BOOST_CHECK_THROW(task::syncWait(reader->range()), std::logic_error);

    // The rejected write really did not land, and reads still work.
    auto valueA = task::syncWait(reader->readOne(hashA));
    BOOST_REQUIRE(valueA.has_value());
    BOOST_CHECK(*valueA == nodeRlpA());
    BOOST_CHECK(!task::syncWait(reader->readOne(hashB)).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
