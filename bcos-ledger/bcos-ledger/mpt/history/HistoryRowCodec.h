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
 * @brief Byte-exact layout of the four reverse-history row kinds (spec B.2)
 *
 * ```text
 *   index    <table>: <logical key bytes> <block 8B big endian>  ->  <1B tag> <old value>
 *   manifest <table>: <block 8B big endian> <shard 2B big endian> ->  [<len 4B BE> <key>, ...]
 *
 *   tag 0x00 = the key did not exist before this block (ABSENT), no bytes follow
 *   tag 0x01 = the old value bytes follow
 * ```
 */
#pragma once

#include "../Errors.h"
#include "HistoryErrors.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
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
/// the query outright (spec B.2(b)).
inline constexpr std::size_t kBlockNumberBytes = 8;
/// Shard ordinal inside one block's manifest, 2 bytes big endian.
inline constexpr std::size_t kShardIndexBytes = 2;
/// Manifest row key = BE64(block) ‖ BE16(shard).
inline constexpr std::size_t kManifestRowKeyBytes = kBlockNumberBytes + kShardIndexBytes;
/// Length prefix of one manifest record, 4 bytes big endian.
inline constexpr std::size_t kRecordLengthBytes = 4;
/// The tag byte that opens every index row value.
inline constexpr std::size_t kTagBytes = 1;
/// tag 0x00: the key did not exist before the indexed block.
inline constexpr char kTagAbsent = '\x00';
/// tag 0x01: the bytes after the tag are the key's value before the indexed block.
inline constexpr char kTagValue = '\x01';
/// The largest shard ordinal a 2-byte field can name.
inline constexpr std::size_t kMaxShardIndex = 0xFFFF;

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

/// A block number as it is written into a row key. Rejects negatives rather than letting them
/// wrap into the top of the unsigned range, where they would sort above every real block.
inline uint64_t toRowBlockNumber(int64_t block)
{
    if (block < 0)
    {
        BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                  "history block number must not be negative"));
    }
    return static_cast<uint64_t>(block);
}

/// Index row key: `<logical key bytes> <BE64 block>`. No length prefix — see
/// indexRowBelongsTo for why none is needed (spec B.2(c)).
inline std::string indexRowKey(std::span<const bcos::byte> key, int64_t block)
{
    std::string rowKey;
    rowKey.reserve(key.size() + kBlockNumberBytes);
    rowKey.append(asStringView(key));
    appendBigEndian<kBlockNumberBytes>(rowKey, toRowBlockNumber(block));
    return rowKey;
}

/// Manifest row key: `<BE64 block> <BE16 shard>`.
inline std::string manifestRowKey(int64_t block, std::size_t shard)
{
    if (shard > kMaxShardIndex)
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation() << bcos::errinfo_comment(
                "history manifest shard ordinal overflows the 2-byte shard field"));
    }
    std::string rowKey;
    rowKey.reserve(kManifestRowKeyBytes);
    appendBigEndian<kBlockNumberBytes>(rowKey, toRowBlockNumber(block));
    appendBigEndian<kShardIndexBytes>(rowKey, static_cast<uint64_t>(shard));
    return rowKey;
}

/// Index row value: the tag byte, followed by the old value when there is one.
/// @param oldValue nullopt means the key did not exist before the indexed block.
inline std::string indexRowValue(std::optional<std::span<const bcos::byte>> oldValue)
{
    std::string value;
    if (!oldValue)
    {
        value.push_back(kTagAbsent);
        return value;
    }
    value.reserve(kTagBytes + oldValue->size());
    value.push_back(kTagValue);
    value.append(asStringView(*oldValue));
    return value;
}

/// spec B.2(c): a row key @p rowKey belongs to @p key exactly when it starts with @p key AND is
/// eight bytes longer. That second clause is the whole reason the layout can skip a length
/// prefix: if the row belonged to some other key k', then |k'| = |rowKey| - 8 = |key|, and k' is
/// also the first |key| bytes of rowKey, so k' == key.
inline bool indexRowBelongsTo(std::string_view rowKey, std::string_view key) noexcept
{
    return rowKey.size() == key.size() + kBlockNumberBytes && rowKey.starts_with(key);
}

/// True when @p rowKey is a manifest row of @p block: `<BE64 block> <BE16 shard>`, exactly ten
/// bytes. The same shape argument as indexRowBelongsTo, with both fields fixed-width.
inline bool manifestRowBelongsTo(std::string_view rowKey, int64_t block)
{
    if (rowKey.size() != kManifestRowKeyBytes)
    {
        return false;
    }
    return readBigEndian<kBlockNumberBytes>(rowKey) == toRowBlockNumber(block);
}

/// Append one `<BE32 len> <key>` record to a manifest shard payload.
inline void appendManifestRecord(std::string& payload, std::span<const bcos::byte> key)
{
    if (key.size() > std::numeric_limits<uint32_t>::max())
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                  "history manifest record length overflows its 4-byte field"));
    }
    appendBigEndian<kRecordLengthBytes>(payload, static_cast<uint64_t>(key.size()));
    payload.append(asStringView(key));
}

/// The size one key occupies in a manifest shard payload.
inline std::size_t manifestRecordSize(std::span<const bcos::byte> key) noexcept
{
    return kRecordLengthBytes + key.size();
}

/// Split one manifest shard payload back into the keys it lists. A payload that ends mid-record
/// is an index corruption, not an empty tail: it fails loud (G6) rather than returning a short
/// list that would leave index rows orphaned at expiry.
inline std::vector<bcos::bytes> decodeManifestShard(std::string_view payload)
{
    std::vector<bcos::bytes> keys;
    std::size_t offset = 0;
    while (offset < payload.size())
    {
        auto length =
            static_cast<std::size_t>(readBigEndian<kRecordLengthBytes>(payload.substr(offset)));
        offset += kRecordLengthBytes;
        if (payload.size() - offset < length)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "history manifest shard ends inside a record: declared key length exceeds "
                    "the remaining payload"));
        }
        auto record = payload.substr(offset, length);
        keys.emplace_back(record.begin(), record.end());
        offset += length;
    }
    return keys;
}

}  // namespace bcos::ledger::mpt::history
