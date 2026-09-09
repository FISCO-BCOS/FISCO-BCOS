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
 * @file HistoryRocksDBTest.cpp
 * @brief The same layout over the storage production actually runs on.
 *
 * MemoryStorage orders rows by the (table, rowKey) pair; RocksDB orders the single physical string
 * "<table>:<rowKey>" bytewise, and reconstructs the pair by splitting at its FIRST colon
 * (bcos-storage/StateKVResolver.h:44). The meta-before-shard-0 order that one seek rides on is a
 * property of that comparator, and history row keys carry arbitrary bytes — 0x00 from every BE64
 * block field, 0x3A whenever a state key contains one — so the whole cycle is pinned against the
 * real thing rather than only against the in-memory stand-in.
 */

#include "HistoryTestHelpers.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
#include <bcos-ledger/mpt/history/HistoryTables.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryRocksDBSuite)

namespace
{
using RocksDbHistoryStorage = bcos::storage2::rocksdb::RocksDBStorage2<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::rocksdb::StateKeyResolver,
    bcos::storage2::rocksdb::StateValueResolver>;

struct RocksDbHistoryFixture
{
    std::string path = "./history-rocksdb-" + std::to_string(std::random_device{}());
    std::unique_ptr<::rocksdb::DB> db;

    RocksDbHistoryFixture()
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;
        ::rocksdb::DB* raw = nullptr;
        auto status = ::rocksdb::DB::Open(options, path, &raw);
        BOOST_REQUIRE(status.ok());
        db.reset(raw);
    }
    RocksDbHistoryFixture(const RocksDbHistoryFixture&) = delete;
    RocksDbHistoryFixture(RocksDbHistoryFixture&&) = delete;
    RocksDbHistoryFixture& operator=(const RocksDbHistoryFixture&) = delete;
    RocksDbHistoryFixture& operator=(RocksDbHistoryFixture&&) = delete;
    ~RocksDbHistoryFixture()
    {
        db.reset();
        boost::filesystem::remove_all(path);
    }
};
}  // namespace

/// One end-to-end pass over RocksDB: put, query, restart-and-rebuild, expire. The keys carry a
/// colon and a NUL on purpose.
BOOST_AUTO_TEST_CASE(rocksDbBackendCarriesTheSameLayout)
{
    RocksDbHistoryFixture fixture;
    RocksDbHistoryStorage storage(*fixture.db, bcos::storage2::rocksdb::StateKeyResolver{},
        bcos::storage2::rocksdb::StateValueResolver{});

    // "/tables/c:slot" is the shape a real state key has, colon included; the NUL makes sure
    // nothing along the path treats the key as C string terminated.
    const auto colonKey = std::string("/tables/c:slot") + '\0' + 'x';

    StateHistoryStore live;
    Diff at2;
    at2.change("abc"sv, "abc-at-1"sv).change(colonKey, std::nullopt);
    Diff at10;
    at10.change("abcde"sv, "abcde-at-9"sv).change(colonKey, "colon-at-9"sv);
    // A cap that splits block 2 across two shards, so the walk crosses a shard boundary here too.
    putBlock(live, storage, 2, at2, /*shardCap=*/24);
    putBlock(live, storage, 10, at10);

    // RocksDB's own comparator puts the 8-byte meta key before the 10-byte shard keys of the same
    // block, which is what makes one seek enough for readBlock, expire and rebuild.
    auto const seen = bcos::task::syncWait([&]() -> bcos::task::Task<std::vector<std::string>> {
        std::vector<std::string> keys;
        auto iterator = co_await bcos::storage2::range(storage, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{kStateHistory.shard, metaRowKey(2)});
        while (true)
        {
            auto row = co_await iterator.next();
            if (!row)
            {
                break;
            }
            auto const& [rowKey, rowValue] = *row;
            bcos::executor_v1::StateKeyView view{rowKey};
            if (view.m_table != kStateHistory.shard)
            {
                break;
            }
            keys.emplace_back(view.m_key);
        }
        co_return keys;
    }());
    BOOST_REQUIRE_EQUAL(seen.size(), std::size_t{5});  // block 2: meta + 2 shards; block 10: meta +
                                                       // 1
    BOOST_CHECK_EQUAL(seen[0], metaRowKey(2));
    BOOST_CHECK_EQUAL(seen[1], shardRowKey(2, 0));
    BOOST_CHECK_EQUAL(seen[2], shardRowKey(2, 1));
    BOOST_CHECK_EQUAL(seen[3], metaRowKey(10));
    BOOST_CHECK_EQUAL(seen[4], shardRowKey(10, 0));

    // The colon and the NUL survive the physical-key round trip in both directions.
    auto const colonAtOne = readAt(live, storage, colonKey, 1, 20, 100);
    BOOST_CHECK(std::holds_alternative<HistoryAbsent>(colonAtOne));
    auto const colonAtNine = readAt(live, storage, colonKey, 9, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(colonAtNine));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(colonAtNine)), "colon-at-9");
    auto const abcdeAtFive = readAt(live, storage, "abcde"sv, 5, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(abcdeAtFive));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(abcdeAtFive)), "abcde-at-9");

    // A restart: a fresh store over the same database walks the shards and answers identically.
    StateHistoryStore restarted;
    auto const report = bcos::task::syncWait(restarted.rebuild(storage));
    BOOST_CHECK_EQUAL(report.blocks, std::size_t{2});
    BOOST_CHECK_EQUAL(report.records, std::size_t{4});
    BOOST_CHECK_EQUAL(restarted.index().versionCount(), std::size_t{4});
    BOOST_CHECK(readAt(restarted, storage, colonKey, 9, 20, 100) == colonAtNine);
    BOOST_CHECK(readAt(restarted, storage, colonKey, 1, 20, 100) == colonAtOne);
    BOOST_CHECK(readAt(restarted, storage, "abcde"sv, 5, 20, 100) == abcdeAtFive);

    // Block 2's whole diff, in shard order across the split.
    auto const block2 = readBlock(restarted, storage, 2);
    BOOST_REQUIRE_EQUAL(block2.records.size(), std::size_t{2});
    BOOST_CHECK_EQUAL(toText(block2.records[0].first), "abc");
    BOOST_CHECK_EQUAL(toText(block2.records[1].first), colonKey);
    BOOST_CHECK(!block2.records[1].second.has_value());

    // Expiry, and the neighbouring block left intact.
    auto const expired =
        bcos::task::syncWait(restarted.expire(storage, storage, 2, RetentionBoundary::Advance));
    BOOST_CHECK_EQUAL(expired.keyCount, std::size_t{2});
    BOOST_CHECK_EQUAL(expired.shardsDeleted, std::size_t{2});
    BOOST_REQUIRE(expired.retired.has_value());
    BOOST_CHECK_EQUAL(*boundaryOnDisk(restarted, storage), 2);
    BOOST_CHECK_EQUAL(readBlock(restarted, storage, 10).records.size(), std::size_t{2});

    // The next restart picks the boundary up and starts above it, so block 2's answers are gone
    // for good rather than reappearing from rows that were never deleted.
    StateHistoryStore afterExpiry;
    auto const secondReport = bcos::task::syncWait(afterExpiry.rebuild(storage));
    BOOST_CHECK_EQUAL(secondReport.blocks, std::size_t{1});
    BOOST_CHECK(!afterExpiry.recordedBlock(2));
    BOOST_CHECK(afterExpiry.recordedBlock(10));
    auto const stillAnswered = readAt(afterExpiry, storage, "abcde"sv, 5, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(stillAnswered));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(stillAnswered)), "abcde-at-9");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
