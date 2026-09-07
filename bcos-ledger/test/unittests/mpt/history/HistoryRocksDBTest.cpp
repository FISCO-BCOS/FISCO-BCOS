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
 * MemoryStorage orders rows by the (table, rowKey) pair; RocksDB orders the single physical
 * string "<table>:<rowKey>" bytewise, and reconstructs the pair by splitting at its FIRST colon
 * (bcos-storage/StateKVResolver.h:44). History row keys carry arbitrary bytes — 0x00 from every
 * BE64 block field, and 0x3A whenever a state key happens to contain one — so the seek order and
 * the key round trip are worth pinning against the real thing rather than only against the
 * in-memory stand-in.
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
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <variant>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryIndexSuite)

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

/// One end-to-end pass over RocksDB: put, read back across the length discriminator, sweep the
/// manifest, expire. The keys carry a colon and a NUL on purpose.
BOOST_AUTO_TEST_CASE(rocksDbBackendCarriesTheSameLayout)
{
    RocksDbHistoryFixture fixture;
    RocksDbHistoryStorage storage(*fixture.db, bcos::storage2::rocksdb::StateKeyResolver{},
        bcos::storage2::rocksdb::StateValueResolver{});

    // "/tables/c:slot" is the shape a real state key has, colon included; the NUL makes sure
    // nothing along the path treats the key as C string terminated.
    const auto colonKey = std::string("/tables/c:slot") + '\0' + 'x';
    Diff at2;
    at2.change("abc"sv, "abc-at-1"sv);
    at2.change(colonKey, "colon-at-1"sv);
    Diff at10;
    at10.change("abcde"sv, "abcde-at-9"sv);
    putBlock(storage, 2, at2);
    putBlock(storage, 10, at10);

    // Byte ordering under RocksDB's own comparator: a query for "abc" at block 5 seeks to
    // "abc"+BE64(6), and RocksDB really does hand back "abcde"+BE64(10) as the first row at or
    // beyond it. Pinning the landing row keeps the UseCurrent assertion below from being vacuous
    // — it is the length clause rejecting a prefix match, not a seek that found nothing.
    auto firstRowKey = bcos::task::syncWait([&]() -> bcos::task::Task<std::string> {
        auto abcKey = makeBytes("abc"sv);
        auto iterator = co_await bcos::storage2::range(storage, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{kStateHistory.index, indexRowKey(abcKey, 6)});
        auto row = co_await iterator.next();
        if (!row)
        {
            co_return std::string{};
        }
        auto const& [rowKey, rowValue] = *row;
        co_return std::string(bcos::executor_v1::StateKeyView{rowKey}.m_key);
    }());
    BOOST_CHECK_EQUAL(firstRowKey, indexRowKey(makeBytes("abcde"sv), 10));

    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(readAt(storage, "abc"sv, 5, 20, 100)));

    auto abcdeAtFive = readAt(storage, "abcde"sv, 5, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(abcdeAtFive));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(abcdeAtFive)), "abcde-at-9");

    // The colon and the NUL survive the physical-key round trip in both directions.
    auto colonAtOne = readAt(storage, colonKey, 1, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(colonAtOne));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(colonAtOne)), "colon-at-1");

    auto block2Keys = keysOfBlock(storage, 2);
    BOOST_REQUIRE_EQUAL(block2Keys.size(), 2);
    BOOST_CHECK_EQUAL(toText(block2Keys[0]), "abc");
    BOOST_CHECK_EQUAL(toText(block2Keys[1]), colonKey);

    // Expiry, and the neighbouring block left intact.
    auto report = expireBlock(storage, 2);
    BOOST_CHECK_EQUAL(report.keyCount, 2);
    BOOST_CHECK_EQUAL(report.manifestShardsDeleted, 1);
    BOOST_CHECK(keysOfBlock(storage, 2).empty());
    BOOST_CHECK_EQUAL(keysOfBlock(storage, 10).size(), 1);

    auto abcdeAfterExpiry = readAt(storage, "abcde"sv, 5, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(abcdeAfterExpiry));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(abcdeAfterExpiry)), "abcde-at-9");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
