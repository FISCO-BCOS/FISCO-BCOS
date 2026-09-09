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
 * @file HistoryRowCodec.h
 * @brief Byte-exact layout of the three reverse-history row kinds (layout spec §1.2)
 *
 * ```text
 *   meta      <shard table>: <BE64 block>                 -> <u8 version=1> <BE32 shardCount>
 *                                                            <BE32 recordCount> <32B blockHash>
 *   shard     <shard table>: <BE64 block> <BE16 shard>    -> record*
 *   boundary  <boundary table>: "boundary"                -> <BE64 block>
 *
 *   record = <BE32 keyLen> <key> <u8 tag> [<BE32 valueLen> <value>]
 *   tag 0x00 = the key did not exist before this block (ABSENT), nothing follows
 *   tag 0x01 = a length-prefixed old value follows
 * ```
 *
 * The old value now lives in the shard record, not in a separate index row: that is what lets one
 * block's whole diff be deleted with `shardCount + 1` removes instead of one per key, and what
 * lets the in-memory query index be REBUILT from the shards at startup (HistoryIndex.h). A
 * record's `offset` — its byte position inside the shard payload, pointing at the `BE32 keyLen` —
 * is the third component of the index's locator.
 */
#pragma once

#include "../Errors.h"
#include "HistoryErrors.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// Block numbers are 8 bytes big endian so that byte order equals numeric order — the property
/// every seek in this component stands on. Little endian would make 255 sort after 256 and break
/// both the query and the rebuild walk (spec B.2(b)).
inline constexpr std::size_t kBlockNumberBytes = 8;
/// Shard ordinal inside one block, 2 bytes big endian.
inline constexpr std::size_t kShardIndexBytes = 2;
/// Meta row key = BE64(block). Eight bytes, and the length IS the discriminator.
inline constexpr std::size_t kMetaRowKeyBytes = kBlockNumberBytes;
/// Shard row key = BE64(block) ‖ BE16(shard).
inline constexpr std::size_t kShardRowKeyBytes = kBlockNumberBytes + kShardIndexBytes;
/// Length prefix of a record's key and of its value, 4 bytes big endian each.
inline constexpr std::size_t kRecordLengthBytes = 4;
/// The tag byte that follows a record's key.
inline constexpr std::size_t kTagBytes = 1;
/// tag 0x00: the key did not exist before the recorded block.
inline constexpr char kTagAbsent = '\x00';
/// tag 0x01: a length-prefixed old value follows.
inline constexpr char kTagValue = '\x01';
/// The largest shard ordinal a 2-byte field can name.
inline constexpr std::size_t kMaxShardIndex = 0xFFFF;
/// The only meta-row format this build understands. A row carrying anything else is refused
/// rather than guessed at — this byte is the gate a future layout change goes through.
inline constexpr char kMetaFormatVersion = '\x01';
/// Meta row value: version ‖ BE32 shardCount ‖ BE32 recordCount ‖ 32-byte block hash. Fixed
/// width, so a short or long row is a corruption the decoder can name.
inline constexpr std::size_t kMetaRowValueBytes =
    1 + kRecordLengthBytes + kRecordLengthBytes + bcos::h256::SIZE;
static_assert(kMetaRowValueBytes == 41, "layout spec §1.2 pins the meta row at 41 bytes");

/// One block's meta row, decoded. `shardCount` and `recordCount` are what make a truncated or
/// partially-expired block detectable during the rebuild walk: the walk counts what it actually
/// reads and compares.
struct BlockMeta
{
    uint32_t shardCount{};
    uint32_t recordCount{};
    bcos::h256 blockHash;

    friend bool operator==(BlockMeta const&, BlockMeta const&) noexcept = default;
};

/// Reinterpret a byte span as the character view every storage key/value API in this repo takes.
/// A view, not a copy — the caller keeps ownership.
inline std::string_view asStringView(std::span<const bcos::byte> bytes) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Append @p value as exactly @p Width big-endian bytes.
template <std::size_t Width>
inline void appendBigEndian(std::string& out, uint64_t value)
{
    std::array<char, Width> buffer{};
    bcos::toBigEndian(value, buffer);
    out.append(buffer.data(), buffer.size());
}

/// Read exactly @p Width big-endian bytes from the front of @p bytes.
template <std::size_t Width>
inline uint64_t readBigEndian(std::string_view bytes)
{
    if (bytes.size() < Width)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history row truncated: fewer bytes than a big-endian field"));
    }
    return bcos::fromBigEndian<uint64_t>(bytes.substr(0, Width));
}

/// A block number as it is written into a row key. Rejects negatives rather than letting them wrap
/// into the top of the unsigned range, where they would sort above every real block.
inline uint64_t toRowBlockNumber(int64_t block)
{
    if (block < 0)
    {
        BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                  "history block number must not be negative"));
    }
    return static_cast<uint64_t>(block);
}

/// The single row key the retention-boundary table holds. A fixed literal: the table has exactly
/// one row, so nothing has to be encoded into its key, and a name rather than an empty string
/// makes the row legible in a dump.
inline constexpr std::string_view kRetentionBoundaryRowKey = "boundary";

/// Retention-boundary row value: BE64 of the oldest block the store can still answer for.
inline std::string retentionBoundaryValue(int64_t oldestIntactBlock)
{
    std::string value;
    value.reserve(kBlockNumberBytes);
    appendBigEndian<kBlockNumberBytes>(value, toRowBlockNumber(oldestIntactBlock));
    return value;
}

/// Inverse of retentionBoundaryValue.
/// @throws MPTInvariantViolation on a row that is not exactly one BE64 field — a boundary the
/// reader cannot trust must not be read as a permissive one.
inline int64_t decodeRetentionBoundary(std::string_view value)
{
    if (value.size() != kBlockNumberBytes)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history retention-boundary row is not a single BE64 field"));
    }
    return static_cast<int64_t>(readBigEndian<kBlockNumberBytes>(value));
}

/// Meta row key: `<BE64 block>`, eight bytes.
inline std::string metaRowKey(int64_t block)
{
    std::string rowKey;
    rowKey.reserve(kMetaRowKeyBytes);
    appendBigEndian<kBlockNumberBytes>(rowKey, toRowBlockNumber(block));
    return rowKey;
}

/// Shard row key: `<BE64 block> <BE16 shard>`, ten bytes. Two bytes longer than the meta row key
/// of the same block, and sharing its first eight bytes, so one seek at the meta key walks meta
/// then shard 0, 1, ...
inline std::string shardRowKey(int64_t block, std::size_t shard)
{
    if (shard > kMaxShardIndex)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history shard ordinal overflows the 2-byte shard field"));
    }
    std::string rowKey;
    rowKey.reserve(kShardRowKeyBytes);
    appendBigEndian<kBlockNumberBytes>(rowKey, toRowBlockNumber(block));
    appendBigEndian<kShardIndexBytes>(rowKey, static_cast<uint64_t>(shard));
    return rowKey;
}

/// Meta row value, the 41 fixed bytes.
inline std::string metaRowValue(
    uint32_t shardCount, uint32_t recordCount, bcos::h256 const& blockHash)
{
    std::string value;
    value.reserve(kMetaRowValueBytes);
    value.push_back(kMetaFormatVersion);
    appendBigEndian<kRecordLengthBytes>(value, shardCount);
    appendBigEndian<kRecordLengthBytes>(value, recordCount);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    value.append(reinterpret_cast<const char*>(blockHash.data()), bcos::h256::SIZE);
    return value;
}

/// Inverse of metaRowValue.
/// @throws MPTInvariantViolation on a wrong length or an unknown format version. The version byte
/// is the gate a future layout change goes through: a reader that guessed at an unknown version
/// would report counts that do not describe the shards it is about to walk.
inline BlockMeta decodeMeta(std::string_view value)
{
    if (value.size() != kMetaRowValueBytes)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                              << bcos::errinfo_comment("history meta row is not exactly 41 bytes"));
    }
    if (value.front() != kMetaFormatVersion)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history meta row carries an unknown format version"));
    }
    BlockMeta meta;
    meta.shardCount = static_cast<uint32_t>(readBigEndian<kRecordLengthBytes>(value.substr(1)));
    meta.recordCount = static_cast<uint32_t>(
        readBigEndian<kRecordLengthBytes>(value.substr(1 + kRecordLengthBytes)));
    auto const hashBytes = value.substr(1 + kRecordLengthBytes + kRecordLengthBytes);
    std::copy(hashBytes.begin(), hashBytes.end(), meta.blockHash.data());
    return meta;
}

/// True when @p rowKey is the meta row of @p block. Length first: an 8-byte key in the shard
/// table is a meta row and nothing else.
inline bool isMetaRowKey(std::string_view rowKey, int64_t block)
{
    return rowKey.size() == kMetaRowKeyBytes &&
           readBigEndian<kBlockNumberBytes>(rowKey) == toRowBlockNumber(block);
}

/// True when @p rowKey is a shard row of @p block: ten bytes whose first eight name the block.
inline bool isShardRowKey(std::string_view rowKey, int64_t block)
{
    return rowKey.size() == kShardRowKeyBytes &&
           readBigEndian<kBlockNumberBytes>(rowKey) == toRowBlockNumber(block);
}

/// The block a meta or shard row key names, whatever its length. Used by the rebuild walk, which
/// does not know the block up front — it discovers it from the rows.
inline int64_t rowKeyBlock(std::string_view rowKey)
{
    if (rowKey.size() != kMetaRowKeyBytes && rowKey.size() != kShardRowKeyBytes)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history shard-table row key is neither 8 nor 10 bytes"));
    }
    return static_cast<int64_t>(readBigEndian<kBlockNumberBytes>(rowKey));
}

/// The shard ordinal a 10-byte shard row key names.
inline std::size_t rowKeyShard(std::string_view rowKey)
{
    if (rowKey.size() != kShardRowKeyBytes)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                              << bcos::errinfo_comment("history shard row key is not ten bytes"));
    }
    return static_cast<std::size_t>(
        readBigEndian<kShardIndexBytes>(rowKey.substr(kBlockNumberBytes)));
}

/// The number of bytes one record occupies in a shard payload. Callers size their shards with
/// this BEFORE appending, so it must agree with appendRecord byte for byte.
inline std::size_t recordSize(
    std::span<const bcos::byte> key, std::optional<std::span<const bcos::byte>> oldValue) noexcept
{
    return kRecordLengthBytes + key.size() + kTagBytes +
           (oldValue ? kRecordLengthBytes + oldValue->size() : 0);
}

/// Append one record to a shard payload and return the OFFSET it starts at — the byte position of
/// its `BE32 keyLen`, which is what the in-memory index stores so a query can decode exactly this
/// record without scanning the shard.
inline std::size_t appendRecord(std::string& payload, std::span<const bcos::byte> key,
    std::optional<std::span<const bcos::byte>> oldValue)
{
    if (key.size() > std::numeric_limits<uint32_t>::max() ||
        (oldValue && oldValue->size() > std::numeric_limits<uint32_t>::max()))
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history record length overflows its 4-byte field"));
    }
    auto const offset = payload.size();
    appendBigEndian<kRecordLengthBytes>(payload, static_cast<uint64_t>(key.size()));
    payload.append(asStringView(key));
    if (!oldValue)
    {
        payload.push_back(kTagAbsent);
        return offset;
    }
    payload.push_back(kTagValue);
    appendBigEndian<kRecordLengthBytes>(payload, static_cast<uint64_t>(oldValue->size()));
    payload.append(asStringView(*oldValue));
    return offset;
}

/// One decoded record. The two byte fields are VIEWS into the payload they were decoded from —
/// the caller copies if it needs to outlive the row.
struct DecodedRecord
{
    std::string_view key;
    /// nullopt when the record is tagged ABSENT: the key did not exist before the block.
    std::optional<std::string_view> oldValue;
    /// Where the NEXT record starts, so a whole-shard walk needs no second length computation.
    std::size_t nextOffset{};
};

/// Decode the record at @p offset. Every failure is loud (G6): a payload that ends inside a
/// record, an offset past the end, or an unknown tag means the query about to be answered from
/// this record would otherwise get bytes from the wrong place.
inline DecodedRecord decodeRecordAt(std::string_view payload, std::size_t offset)
{
    if (offset > payload.size())
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history record offset points past the end of its shard"));
    }
    DecodedRecord record;
    auto cursor = offset;
    auto const keyLength =
        static_cast<std::size_t>(readBigEndian<kRecordLengthBytes>(payload.substr(cursor)));
    cursor += kRecordLengthBytes;
    if (payload.size() - cursor < keyLength)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                              << bcos::errinfo_comment("history shard ends inside a record key"));
    }
    record.key = payload.substr(cursor, keyLength);
    cursor += keyLength;
    if (cursor >= payload.size())
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                              << bcos::errinfo_comment("history record is missing its tag byte"));
    }
    auto const tag = payload[cursor];
    cursor += kTagBytes;
    if (tag == kTagAbsent)
    {
        record.nextOffset = cursor;
        return record;
    }
    if (tag != kTagValue)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history record carries an unknown tag byte"));
    }
    auto const valueLength =
        static_cast<std::size_t>(readBigEndian<kRecordLengthBytes>(payload.substr(cursor)));
    cursor += kRecordLengthBytes;
    if (payload.size() - cursor < valueLength)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                              << bcos::errinfo_comment("history shard ends inside a record value"));
    }
    record.oldValue = payload.substr(cursor, valueLength);
    record.nextOffset = cursor + valueLength;
    return record;
}

/// One record as the whole-shard walk sees it: the decoded fields plus where it started, which is
/// what the rebuild stores in the index.
struct ShardRecord
{
    std::string_view key;
    std::optional<std::string_view> oldValue;
    std::size_t offset{};
};

/// Split a whole shard payload into its records, in payload order. Views into @p payload.
inline std::vector<ShardRecord> decodeShard(std::string_view payload)
{
    std::vector<ShardRecord> records;
    std::size_t offset = 0;
    while (offset < payload.size())
    {
        auto decoded = decodeRecordAt(payload, offset);
        records.push_back(
            ShardRecord{.key = decoded.key, .oldValue = decoded.oldValue, .offset = offset});
        offset = decoded.nextOffset;
    }
    return records;
}

}  // namespace bcos::ledger::mpt::history
