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
 * @file HistoryLayoutTest.cpp
 * @brief The bytes themselves: the 41-byte meta row, the record encoding, the meta-before-shard-0
 *        sort order one seek rides on, and the shard split (layout spec §1.2)
 */

#include "HistoryTestHelpers.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
#include <bcos-ledger/mpt/history/HistoryTables.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryLayoutSuite)

namespace
{
/// `<BE32 keyLen> <key> <tag> [<BE32 valueLen> <value>]`, spelled out by hand so the assertions
/// below do not merely re-run the encoder they are checking.
std::string expectedRecord(std::string_view key, std::optional<std::string_view> oldValue)
{
    std::string record;
    record.append("\x00\x00\x00", 3);
    record.push_back(static_cast<char>(key.size()));
    record.append(key);
    if (!oldValue)
    {
        record.push_back('\x00');
        return record;
    }
    record.push_back('\x01');
    record.append("\x00\x00\x00", 3);
    record.push_back(static_cast<char>(oldValue->size()));
    record.append(*oldValue);
    return record;
}
}  // namespace

/// The meta row is 41 fixed bytes: version, shard count, record count, block hash. A reader that
/// found a shorter or longer row would be reading counts out of the wrong offsets, so the length is
/// asserted before the fields.
BOOST_AUTO_TEST_CASE(metaRowIsFortyOneFixedBytes)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("alpha"sv, "old-a"sv).change("beta"sv, std::nullopt);
    putBlock(store, storage, 7, diff);

    auto const rows = rowsOfTable(storage, kStateHistory.shard);
    BOOST_REQUIRE_EQUAL(rows.size(), 2);  // meta + shard 0
    BOOST_CHECK_EQUAL(rows[0].first, metaRowKey(7));
    BOOST_CHECK_EQUAL(rows[0].first.size(), 8);
    BOOST_REQUIRE_EQUAL(rows[0].second.size(), 41);

    std::string expected;
    expected.push_back('\x01');                // format version
    expected.append("\x00\x00\x00\x01", 4);    // shardCount = 1
    expected.append("\x00\x00\x00\x02", 4);    // recordCount = 2
    expected.append(std::string(30, '\x00'));  // block hash, first 30 bytes
    expected.push_back('\x00');
    expected.push_back('\x07');
    BOOST_CHECK_EQUAL(rows[0].second, expected);

    auto const meta = decodeMeta(rows[0].second);
    BOOST_CHECK_EQUAL(meta.shardCount, 1);
    BOOST_CHECK_EQUAL(meta.recordCount, 2);
    BOOST_CHECK(meta.blockHash == blockHashOf(7));
}

/// The record encoding, byte for byte, including the two cases that must stay distinguishable:
/// tag 0x00 (the key did not exist yet) and tag 0x01 with an EMPTY value. Collapsing them would
/// make "account created in this block" read back as "account held the empty string".
BOOST_AUTO_TEST_CASE(recordEncodingIsByteExact)
{
    // A key carrying both bytes that have bitten this layout before: 0x00 (every BE64 block field
    // is full of them) and 0x3A, the colon StateKeyResolver splits physical keys on.
    auto const awkwardKey = std::string("/t/c:s") + '\0' + 'x';

    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("created"sv, std::nullopt).change("emptied"sv, ""sv).change(awkwardKey, "old"sv);
    putBlock(store, storage, 3, diff);

    auto const rows = rowsOfTable(storage, kStateHistory.shard);
    BOOST_REQUIRE_EQUAL(rows.size(), 2);
    BOOST_CHECK_EQUAL(rows[1].first, shardRowKey(3, 0));
    BOOST_CHECK_EQUAL(rows[1].first.size(), 10);

    auto const expected = expectedRecord("created"sv, std::nullopt) +
                          expectedRecord("emptied"sv, ""sv) + expectedRecord(awkwardKey, "old"sv);
    BOOST_CHECK_EQUAL(rows[1].second, expected);

    // ...and the decoder puts the same three records back, with ABSENT still distinct from empty.
    auto const decoded = decodeShard(rows[1].second);
    BOOST_REQUIRE_EQUAL(decoded.size(), 3);
    BOOST_CHECK_EQUAL(decoded[0].key, "created");
    BOOST_CHECK(!decoded[0].oldValue.has_value());
    BOOST_CHECK_EQUAL(decoded[0].offset, std::size_t{0});
    BOOST_CHECK_EQUAL(decoded[1].key, "emptied");
    BOOST_REQUIRE(decoded[1].oldValue.has_value());
    BOOST_CHECK(decoded[1].oldValue->empty());
    BOOST_CHECK_EQUAL(decoded[2].key, awkwardKey);
    BOOST_REQUIRE(decoded[2].oldValue.has_value());
    BOOST_CHECK_EQUAL(*decoded[2].oldValue, "old");
    // Each offset is where its record starts, which is what the index stores.
    BOOST_CHECK_EQUAL(decoded[1].offset, expectedRecord("created"sv, std::nullopt).size());
    BOOST_CHECK_EQUAL(decoded[2].offset, expectedRecord("created"sv, std::nullopt).size() +
                                             expectedRecord("emptied"sv, ""sv).size());
}

/// The whole one-seek property: an 8-byte meta key sorts before every 10-byte shard key sharing its
/// first eight bytes, so ONE seek at BE64(block) walks meta, shard 0, shard 1, ... This is what
/// readBlock, expire and rebuild all ride on, and it is a property of the STORAGE's comparator, so
/// it is checked against a real seek rather than against std::string::operator<.
BOOST_AUTO_TEST_CASE(metaRowSortsBeforeShardZeroUnderASeek)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    for (int index = 0; index < 5; ++index)
    {
        diff.change("key-" + std::to_string(index) + "!!!", "old"sv);  // 8-byte keys
    }
    // recordSize = 4 + 8 + 1 + 4 + 3 = 20, so a 45-byte cap fits two records per shard.
    constexpr std::size_t kSplitCap = 45;
    putBlock(store, storage, 42, diff, kSplitCap);

    auto const seen = bcos::task::syncWait([&]() -> bcos::task::Task<std::vector<std::string>> {
        std::vector<std::string> keys;
        auto iterator = co_await bcos::storage2::range(storage, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{kStateHistory.shard, metaRowKey(42)});
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

    BOOST_REQUIRE_EQUAL(seen.size(), 4);  // meta + 3 shards
    BOOST_CHECK_EQUAL(seen[0], metaRowKey(42));
    BOOST_CHECK_EQUAL(seen[1], shardRowKey(42, 0));
    BOOST_CHECK_EQUAL(seen[2], shardRowKey(42, 1));
    BOOST_CHECK_EQUAL(seen[3], shardRowKey(42, 2));

    // The split really happened at the cap, and the meta row says so.
    auto const rows = rowsOfTable(storage, kStateHistory.shard);
    BOOST_CHECK_EQUAL(decodeMeta(rows[0].second).shardCount, 3);
    BOOST_CHECK_EQUAL(decodeMeta(rows[0].second).recordCount, 5);
    for (std::size_t shard = 0; shard < 3; ++shard)
    {
        BOOST_CHECK_LE(rows[shard + 1].second.size(), kSplitCap);
    }
}

/// The cap bounds a shard, it cannot split a record: a key whose own record is larger than the cap
/// still gets one shard to itself rather than looping forever trying to fit.
BOOST_AUTO_TEST_CASE(oversizedRecordGetsItsOwnShard)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    std::string const longKey(100, 'k');
    Diff diff;
    diff.change(longKey, "old"sv);
    putBlock(store, storage, 3, diff, /*shardCap=*/30);

    auto const rows = rowsOfTable(storage, kStateHistory.shard);
    BOOST_REQUIRE_EQUAL(rows.size(), 2);
    BOOST_CHECK_EQUAL(decodeMeta(rows[0].second).shardCount, 1);
    // 4 (key length) + 100 (key) + 1 (tag) + 4 (value length) + 3 (value) = 112 > the 30-byte cap.
    BOOST_CHECK_EQUAL(rows[1].second.size(), std::size_t{112});
}

/// spec B.10 ②: the audit reads the window as one meta row per block and treats a gap as fatal, so
/// a block that changed nothing must still say so — Meta{shardCount = 1, recordCount = 0} plus an
/// empty shard 0, not an absent block.
BOOST_AUTO_TEST_CASE(emptyBlockGetsMetaAndAnEmptyShardZero)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff const nothingChanged;
    putBlock(store, storage, 11, nothingChanged);

    auto const rows = rowsOfTable(storage, kStateHistory.shard);
    BOOST_REQUIRE_EQUAL(rows.size(), 2);
    auto const meta = decodeMeta(rows[0].second);
    BOOST_CHECK_EQUAL(meta.shardCount, 1);
    BOOST_CHECK_EQUAL(meta.recordCount, 0);
    BOOST_CHECK_EQUAL(rows[1].first, shardRowKey(11, 0));
    BOOST_CHECK(rows[1].second.empty());

    BOOST_CHECK(store.recordedBlock(11));
    BOOST_CHECK(!store.recordedBlock(12));
    BOOST_CHECK(readBlock(store, storage, 11).records.empty());
}

/// spec B.2(b): the block field is 8 bytes big endian so that byte order equals numeric order.
/// 255 -> 256 is the smallest carry a little-endian field would invert, so it is the case that pins
/// the encoding down — and the shard walk is what would break.
BOOST_AUTO_TEST_CASE(bigEndianBlockNumbersDoNotInvertAt255)
{
    BOOST_CHECK_EQUAL(metaRowKey(255), std::string("\x00\x00\x00\x00\x00\x00\x00\xff", 8));
    BOOST_CHECK_EQUAL(metaRowKey(256), std::string("\x00\x00\x00\x00\x00\x00\x01\x00", 8));
    BOOST_CHECK(metaRowKey(255) < metaRowKey(256));
    BOOST_CHECK(metaRowKey(255) < shardRowKey(255, 0));
    BOOST_CHECK(shardRowKey(255, 0xFFFF) < metaRowKey(256));

    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff at255;
    at255.change("balance"sv, "at-254"sv);
    Diff at256;
    at256.change("balance"sv, "at-255"sv);
    putBlock(store, storage, 255, at255);
    putBlock(store, storage, 256, at256);

    auto const result = readAt(store, storage, "balance"sv, 254, 300, 300);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(result));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(result)), "at-254");
}

/// spec B.4: the write path is pure append. Not one read, not one seek — that is what keeps write
/// amplification equal to the block's diff instead of growing with the window depth.
BOOST_AUTO_TEST_CASE(putNeitherReadsNorSeeks)
{
    CountingStorage backend;
    StateHistoryStore store;
    Diff diff;
    for (int index = 0; index < 20; ++index)
    {
        diff.change("key-" + std::to_string(index), "old-" + std::to_string(index));
    }
    putBlock(store, backend, 100, diff, /*shardCap=*/60);

    BOOST_CHECK_EQUAL(backend.readCalls, 0);
    BOOST_CHECK_EQUAL(backend.rangeCalls, 0);
    // One row per shard plus the meta row, and nothing per key — the point of the layout.
    auto const rows = rowsOfTable(backend.inner, kStateHistory.shard);
    BOOST_CHECK_EQUAL(rows.size(), decodeMeta(rows[0].second).shardCount + 1);
    BOOST_CHECK_EQUAL(decodeMeta(rows[0].second).recordCount, 20);
}

/// A key recorded twice in one block would get two index versions for one block, and every later
/// query would resolve to whichever came second. The caller owns the deduplication (spec B.4); the
/// store refuses the input rather than accepting it.
BOOST_AUTO_TEST_CASE(duplicateKeyInOneBlockIsRejected)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("balance"sv, "first"sv).change("balance"sv, "second"sv);
    BOOST_CHECK_THROW(putBlock(store, storage, 5, diff), MPTInvariantViolation);
}

/// The two instantiations (spec B.8) share the code but not the rows: writing state history must
/// leave the trie-history tables empty and vice versa.
BOOST_AUTO_TEST_CASE(stateAndTrieInstancesUseSeparateTables)
{
    HistoryMemStorage storage;
    StateHistoryStore stateStore;
    TrieHistoryStore trieStore;
    Diff stateDiff;
    stateDiff.change("shared-key"sv, "state-old"sv);
    Diff trieDiff;
    trieDiff.change("shared-key"sv, "trie-old"sv);
    putBlock(stateStore, storage, 9, stateDiff);
    putBlock(trieStore, storage, 9, trieDiff);

    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.shard).size(), 2);
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kTrieHistory.shard).size(), 2);

    auto const stateResult = readAt(stateStore, storage, "shared-key"sv, 8, 50, 50);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(stateResult));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(stateResult)), "state-old");
    auto const trieResult = readAt(trieStore, storage, "shared-key"sv, 8, 50, 50);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(trieResult));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(trieResult)), "trie-old");
}

/// The retention-boundary row lives in its OWN table, and that is what keeps it out of every shard
/// walk — including the rebuild's, which walks the shard table from the boundary UPWARDS and would
/// otherwise have to skip its own input row.
BOOST_AUTO_TEST_CASE(retentionBoundaryLivesOutsideTheShardTable)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    TrieHistoryStore trieStore;
    Diff at100;
    at100.change("k"sv, "old"sv);
    putBlock(store, storage, 100, at100);
    bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 42));

    auto const boundaryRows = rowsOfTable(storage, kStateHistory.boundary);
    BOOST_REQUIRE_EQUAL(boundaryRows.size(), 1);
    BOOST_CHECK_EQUAL(boundaryRows.front().first, std::string(kRetentionBoundaryRowKey));
    BOOST_CHECK_EQUAL(boundaryRows.front().second, retentionBoundaryValue(42));
    BOOST_CHECK_EQUAL(decodeRetentionBoundary(boundaryRows.front().second), 42);
    auto const readBack = boundaryOnDisk(store, storage);
    BOOST_REQUIRE(readBack.has_value());
    BOOST_CHECK_EQUAL(*readBack, 42);

    // The walked table gained nothing, and its walk still reports exactly the block's own record.
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.shard).size(), 2);
    BOOST_CHECK_EQUAL(readBlock(store, storage, 100).records.size(), 1);

    // And the separate table is LOAD-BEARING under this layout, not merely tidy: "boundary" is
    // eight bytes, exactly the length of a meta row key, so the length discriminator that tells
    // meta rows from shard rows cannot tell a boundary row from a meta row at all. Put it in the
    // shard table and every walker would read it as the meta row of block 0x626F756E64617279.
    BOOST_CHECK_EQUAL(kRetentionBoundaryRowKey.size(), kMetaRowKeyBytes);
    BOOST_CHECK(isMetaRowKey(kRetentionBoundaryRowKey, 0x626F756E64617279));

    // The two instances keep separate boundaries, like their other table.
    BOOST_CHECK(!boundaryOnDisk(trieStore, storage).has_value());
}

/// Every decoder failure the layout can produce, on hand-built bytes. Each of these would
/// otherwise surface as a query answered from the wrong offset (G6).
BOOST_AUTO_TEST_CASE(malformedRowsFailLoud)
{
    // Meta: wrong length, then an unknown format version — the gate a future layout goes through.
    BOOST_CHECK_THROW(decodeMeta(std::string(40, '\x01')), MPTInvariantViolation);
    auto badVersion = metaRowValue(1, 1, blockHashOf(1));
    badVersion[0] = '\x02';
    BOOST_CHECK_THROW(decodeMeta(badVersion), MPTInvariantViolation);

    // Records: truncated key, missing tag, unknown tag, truncated value.
    auto const whole = expectedRecord("abc"sv, "xy"sv);
    BOOST_CHECK_THROW(decodeShard(whole.substr(0, 5)), MPTInvariantViolation);
    BOOST_CHECK_THROW(decodeShard(whole.substr(0, 7)), MPTInvariantViolation);
    auto unknownTag = whole;
    unknownTag[7] = '\x09';
    BOOST_CHECK_THROW(decodeShard(unknownTag), MPTInvariantViolation);
    BOOST_CHECK_THROW(decodeShard(whole.substr(0, whole.size() - 1)), MPTInvariantViolation);
    // ...and an offset that points past the end of its shard.
    BOOST_CHECK_THROW(decodeRecordAt(whole, whole.size() + 1), MPTInvariantViolation);

    // A row key in the shard table that is neither 8 nor 10 bytes has no reading at all.
    BOOST_CHECK_THROW(rowKeyBlock(std::string(9, '\x00')), MPTInvariantViolation);
    BOOST_CHECK(isMetaRowKey(metaRowKey(5), 5));
    BOOST_CHECK(!isMetaRowKey(shardRowKey(5, 0), 5));
    BOOST_CHECK(isShardRowKey(shardRowKey(5, 0), 5));
    BOOST_CHECK(!isShardRowKey(shardRowKey(6, 0), 5));

    // A zero shard cap would make the split loop meaningless rather than merely tight.
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("k"sv, "v"sv);
    BOOST_CHECK_THROW(putBlock(store, storage, 1, diff, /*shardCap=*/0), MPTInvariantViolation);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
