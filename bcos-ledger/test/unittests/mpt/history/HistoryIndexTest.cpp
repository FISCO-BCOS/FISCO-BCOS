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
 * @file HistoryIndexTest.cpp
 * @brief The row layout itself: big-endian block ordering, the length discriminator that lets
 *        the layout skip a length prefix, ABSENT round trip, manifest sharding, and the
 *        append-only write path (spec B.2, B.4)
 */

#include "HistoryTestHelpers.h"
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
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

BOOST_AUTO_TEST_SUITE(HistoryIndexSuite)

/// spec B.2(b): the block field is 8 bytes big endian so that byte order equals numeric order.
/// 255 -> 256 is the smallest carry that a little-endian field would invert (0xFF,0x00.. would
/// sort above 0x00,0x01,0x00..), so it is the case that pins the encoding down.
BOOST_AUTO_TEST_CASE(bigEndianBlockNumbersDoNotInvertAt255)
{
    auto key = makeBytes("balance"sv);
    auto row255 = indexRowKey(key, 255);
    auto row256 = indexRowKey(key, 256);

    BOOST_REQUIRE_EQUAL(row255.size(), key.size() + 8);
    BOOST_REQUIRE_EQUAL(row256.size(), key.size() + 8);
    BOOST_CHECK_EQUAL(
        row255.substr(key.size()), std::string("\x00\x00\x00\x00\x00\x00\x00\xff", 8));
    BOOST_CHECK_EQUAL(
        row256.substr(key.size()), std::string("\x00\x00\x00\x00\x00\x00\x01\x00", 8));
    BOOST_CHECK(row255 < row256);

    // And the ordering is the one the seek actually rides on: with a change at both 255 and 256,
    // a query at 254 must land on the 255 row, not the 256 one.
    HistoryMemStorage storage;
    Diff at255;
    at255.change("balance"sv, "at-254"sv);
    Diff at256;
    at256.change("balance"sv, "at-255"sv);
    putBlock(storage, 255, at255);
    putBlock(storage, 256, at256);

    auto result = readAt(storage, "balance"sv, 254, 300, 300);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(result));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(result)), "at-254");
}

/// spec B.2(c): a row belongs to key k only if it starts with k AND is exactly eight bytes
/// longer. Without the length clause, a query for "abc" whose own next change lies before the
/// queried block walks straight onto "abcde"'s row and answers with another key's value.
BOOST_AUTO_TEST_CASE(lengthDiscriminatorSeparatesPrefixKeys)
{
    HistoryMemStorage storage;
    // "abc" last changed at block 2, i.e. before the query point; "abcde" changed at block 10.
    Diff shortKey;
    shortKey.change("abc"sv, "abc-at-1"sv);
    Diff longerKey;
    longerKey.change("abcde"sv, "abcde-at-9"sv);
    putBlock(storage, 2, shortKey);
    putBlock(storage, 10, longerKey);

    // The seek for "abc" at block 5 starts at "abc" + BE64(6). "abc" + BE64(2) sorts before it,
    // so the first row the iterator yields is "abcde" + BE64(10) — a prefix match, and the wrong
    // answer. The length check rejects it and the caller falls back to the current value.
    auto result = readAt(storage, "abc"sv, 5, 20, 100);
    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(result));

    // Each key still resolves against its own rows.
    auto abcAtZero = readAt(storage, "abc"sv, 0, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(abcAtZero));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(abcAtZero)), "abc-at-1");

    auto abcdeAtFive = readAt(storage, "abcde"sv, 5, 20, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(abcdeAtFive));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(abcdeAtFive)), "abcde-at-9");

    // The predicate on its own, over the exact byte strings above.
    BOOST_CHECK(indexRowBelongsTo(indexRowKey(makeBytes("abc"sv), 2), "abc"sv));
    BOOST_CHECK(!indexRowBelongsTo(indexRowKey(makeBytes("abcde"sv), 10), "abc"sv));
}

/// tag 0x00 (the key did not exist yet) and tag 0x01 (here are the old bytes) must survive the
/// round trip as two distinguishable answers — collapsing ABSENT into an empty value would make
/// "account created in this block" read back as "account held the empty string".
BOOST_AUTO_TEST_CASE(absentAndValueRoundTrip)
{
    HistoryMemStorage storage;
    Diff diff;
    diff.change("created"sv, std::nullopt).change("updated"sv, "old-bytes"sv);
    putBlock(storage, 7, diff);

    auto created = readAt(storage, "created"sv, 6, 50, 50);
    BOOST_CHECK(std::holds_alternative<HistoryAbsent>(created));

    auto updated = readAt(storage, "updated"sv, 6, 50, 50);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(updated));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(updated)), "old-bytes");

    // The bytes on disk, spelled out: <1B tag><old value>.
    auto rows = rowsOfTable(storage, kStateHistory.index);
    BOOST_REQUIRE_EQUAL(rows.size(), 2);
    for (auto const& [rowKey, rowValue] : rows)
    {
        if (rowKey.starts_with("created"))
        {
            BOOST_CHECK_EQUAL(rowValue, std::string("\x00", 1));
        }
        else
        {
            BOOST_CHECK_EQUAL(rowValue, std::string("\x01", 1) + "old-bytes");
        }
    }

    // An empty old value is NOT the same row as ABSENT.
    Diff emptyValueDiff;
    emptyValueDiff.change("emptied"sv, ""sv);
    putBlock(storage, 8, emptyValueDiff);
    auto emptied = readAt(storage, "emptied"sv, 7, 50, 50);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(emptied));
    BOOST_CHECK(std::get<bcos::bytes>(emptied).empty());
}

/// The manifest splits at a byte cap and keysOfBlock rejoins the shards in order (spec B.2).
BOOST_AUTO_TEST_CASE(manifestShardsSplitAtByteCapAndRejoin)
{
    HistoryMemStorage storage;
    Diff diff;
    std::vector<std::string> keys;
    for (int index = 0; index < 5; ++index)
    {
        keys.push_back("key-" + std::to_string(index) + "!!!");  // 8 bytes each
        diff.change(keys.back(), "old"sv);
    }
    // Record size is 4 + 8 = 12, so a 30-byte cap fits two records per shard.
    constexpr std::size_t shardCap = 30;
    putBlock(storage, 42, diff, shardCap);

    auto manifestRows = rowsOfTable(storage, kStateHistory.manifest);
    BOOST_REQUIRE_EQUAL(manifestRows.size(), 3);
    for (std::size_t shard = 0; shard < manifestRows.size(); ++shard)
    {
        BOOST_CHECK_EQUAL(manifestRows[shard].first, manifestRowKey(42, shard));
        BOOST_CHECK_LE(manifestRows[shard].second.size(), shardCap);
    }

    auto rejoined = keysOfBlock(storage, 42);
    BOOST_REQUIRE_EQUAL(rejoined.size(), keys.size());
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        BOOST_CHECK_EQUAL(toText(rejoined[index]), keys[index]);
    }
}

/// The cap bounds a shard, it cannot split a record: a key whose own record is larger than the
/// cap still gets one shard to itself rather than looping forever trying to fit.
BOOST_AUTO_TEST_CASE(oversizedRecordGetsItsOwnShard)
{
    HistoryMemStorage storage;
    std::string longKey(100, 'k');
    Diff diff;
    diff.change(longKey, "old"sv);
    putBlock(storage, 3, diff, /*shardCap=*/30);

    auto manifestRows = rowsOfTable(storage, kStateHistory.manifest);
    BOOST_REQUIRE_EQUAL(manifestRows.size(), 1);
    BOOST_CHECK_EQUAL(manifestRows[0].second.size(), 4 + longKey.size());

    auto rejoined = keysOfBlock(storage, 3);
    BOOST_REQUIRE_EQUAL(rejoined.size(), 1);
    BOOST_CHECK_EQUAL(toText(rejoined[0]), longKey);
}

/// spec B.10 ②: the audit reads the window as "one manifest per block" and treats a gap as
/// fatal, so a block that changed nothing must still say so.
BOOST_AUTO_TEST_CASE(emptyBlockStillGetsAManifest)
{
    HistoryMemStorage storage;
    Diff nothingChanged;
    putBlock(storage, 11, nothingChanged);

    auto manifestRows = rowsOfTable(storage, kStateHistory.manifest);
    BOOST_REQUIRE_EQUAL(manifestRows.size(), 1);
    BOOST_CHECK_EQUAL(manifestRows[0].first, manifestRowKey(11, 0));
    BOOST_CHECK(manifestRows[0].second.empty());
    BOOST_CHECK(keysOfBlock(storage, 11).empty());
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.index).empty());
}

/// spec B.4: the write path is pure append. Not one read, not one seek — that is what keeps
/// write amplification equal to the block's diff instead of growing with the window depth.
BOOST_AUTO_TEST_CASE(putNeitherReadsNorSeeks)
{
    CountingStorage mutableLayer;
    Diff diff;
    for (int index = 0; index < 20; ++index)
    {
        diff.change("key-" + std::to_string(index), "old-" + std::to_string(index));
    }
    putBlock(mutableLayer, 100, diff, /*shardCap=*/40);

    BOOST_CHECK_EQUAL(mutableLayer.readCalls, 0);
    BOOST_CHECK_EQUAL(mutableLayer.rangeCalls, 0);
    // The rows did land: 20 index rows plus the manifest shards.
    BOOST_CHECK_EQUAL(rowsOfTable(mutableLayer.inner, kStateHistory.index).size(), 20);
    BOOST_CHECK(!rowsOfTable(mutableLayer.inner, kStateHistory.manifest).empty());
}

/// A key recorded twice in one block would overwrite the block-start value with a mid-block one,
/// and every later query for that block would silently get the wrong answer. The caller owns the
/// deduplication (spec B.4); the store refuses the input rather than accepting it.
BOOST_AUTO_TEST_CASE(duplicateKeyInOneBlockIsRejected)
{
    HistoryMemStorage storage;
    Diff diff;
    diff.change("balance"sv, "first"sv).change("balance"sv, "second"sv);
    BOOST_CHECK_THROW(putBlock(storage, 5, diff), MPTInvariantViolation);
}

/// The two instantiations (spec B.8) share the code but not the rows: writing state history must
/// leave the trie-history tables empty and vice versa.
BOOST_AUTO_TEST_CASE(stateAndTrieInstancesUseSeparateTables)
{
    HistoryMemStorage storage;
    Diff stateDiff;
    stateDiff.change("shared-key"sv, "state-old"sv);
    Diff trieDiff;
    trieDiff.change("shared-key"sv, "trie-old"sv);
    bcos::task::syncWait(StateHistoryStore::put(storage, 9, stateDiff.entries(), kWideShardCap));
    bcos::task::syncWait(TrieHistoryStore::put(storage, 9, trieDiff.entries(), kWideShardCap));

    auto stateRows = rowsOfTable(storage, kStateHistory.index);
    auto trieRows = rowsOfTable(storage, kTrieHistory.index);
    BOOST_REQUIRE_EQUAL(stateRows.size(), 1);
    BOOST_REQUIRE_EQUAL(trieRows.size(), 1);
    BOOST_CHECK_EQUAL(stateRows[0].second, std::string("\x01", 1) + "state-old");
    BOOST_CHECK_EQUAL(trieRows[0].second, std::string("\x01", 1) + "trie-old");

    auto keyBytes = makeBytes("shared-key"sv);
    auto trieResult = bcos::task::syncWait(TrieHistoryStore::readAt(storage, keyBytes, 8, 50, 50));
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(trieResult));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(trieResult)), "trie-old");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
