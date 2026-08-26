// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Per-height undo journal for one-level tip rollback.
// SYS_REORG_UNDO key = decimal height; value = ReorgUndoBlob
// (pre-block values, /mpt/ excluded, plus tx counters).

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::executor_v1::opstack
{

/// Pre-block value for one key. nullopt means the key did not exist.
struct ReorgUndoRow
{
    executor_v1::StateKey key;
    std::optional<storage::Entry> oldValue;
};

struct ReorgUndoBlob
{
    /// Tx counters of the journaled block (used to undo prewrite totals).
    int64_t txCount = 0;
    int64_t failedCount = 0;
    std::vector<ReorgUndoRow> rows;
};

/// Wire format (version 1, little-endian, no padding):
///   u8 version | u64 txCount | u64 failedCount | u32 rowCount
///   per row: u32 keyLen | key | u8 hasOld | (u32 oldLen | old)?
class ReorgUndoCodec
{
public:
    static constexpr uint8_t kVersion = 1;

    static bcos::bytes encode(ReorgUndoBlob const& blob)
    {
        bcos::bytes out;
        auto pushU64 = [&out](uint64_t v) {
            for (unsigned i = 0; i < sizeof(v); ++i)
            {
                out.push_back(static_cast<bcos::byte>(v >> (i * 8)));
            }
        };
        auto pushU32 = [&out](uint32_t v) {
            for (unsigned i = 0; i < sizeof(v); ++i)
            {
                out.push_back(static_cast<bcos::byte>(v >> (i * 8)));
            }
        };

        out.push_back(kVersion);
        pushU64(static_cast<uint64_t>(blob.txCount));
        pushU64(static_cast<uint64_t>(blob.failedCount));
        if (blob.rows.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::runtime_error("ReorgUndo: row count overflows u32");
        }
        pushU32(static_cast<uint32_t>(blob.rows.size()));
        for (auto const& row : blob.rows)
        {
            auto const& keyBytes = row.key.m_tableAndKey;
            if (keyBytes.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error("ReorgUndo: key length overflows u32");
            }
            pushU32(static_cast<uint32_t>(keyBytes.size()));
            out.insert(out.end(), keyBytes.begin(), keyBytes.end());
            if (row.oldValue.has_value())
            {
                out.push_back(1);
                auto value = (*row.oldValue).get();
                if (value.size() > std::numeric_limits<uint32_t>::max())
                {
                    throw std::runtime_error("ReorgUndo: value length overflows u32");
                }
                pushU32(static_cast<uint32_t>(value.size()));
                out.insert(out.end(), value.begin(), value.end());
            }
            else
            {
                out.push_back(0);
            }
        }
        return out;
    }

    static ReorgUndoBlob decode(std::string_view data)
    {
        auto cursor = data.begin();
        auto remaining = [&data, &cursor]() -> size_t {
            return static_cast<size_t>(std::distance(cursor, data.end()));
        };
        auto take = [&cursor, &remaining](size_t n) -> std::string_view {
            if (remaining() < n)
            {
                throw std::runtime_error("ReorgUndo: truncated blob");
            }
            if (n == 0)
            {
                return {};
            }
            auto begin = cursor;
            std::advance(cursor, static_cast<std::ptrdiff_t>(n));
            return {&*begin, n};
        };
        auto takeU8 = [&take]() -> uint8_t { return static_cast<uint8_t>(take(1)[0]); };
        auto takeU32 = [&take]() -> uint32_t {
            auto bytes = take(4);
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[i])) << (i * 8);
            }
            return value;
        };
        auto takeU64 = [&take]() -> uint64_t {
            auto bytes = take(8);
            uint64_t value = 0;
            for (unsigned i = 0; i < 8; ++i)
            {
                value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[i])) << (i * 8);
            }
            return value;
        };

        if (takeU8() != kVersion)
        {
            throw std::runtime_error("ReorgUndo: unsupported blob version");
        }
        ReorgUndoBlob blob;
        auto const txCountU64 = takeU64();
        auto const failedCountU64 = takeU64();
        auto const maxI64 = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        if (txCountU64 > maxI64 || failedCountU64 > maxI64)
        {
            throw std::runtime_error("ReorgUndo: counter exceeds int64");
        }
        blob.txCount = static_cast<int64_t>(txCountU64);
        blob.failedCount = static_cast<int64_t>(failedCountU64);
        auto rowCount = takeU32();
        constexpr size_t kMinRowBytes = 5;  // u32 keyLen + u8 hasOld
        if (rowCount > 0 && remaining() < static_cast<size_t>(rowCount) * kMinRowBytes)
        {
            throw std::runtime_error("ReorgUndo: truncated blob");
        }
        blob.rows.reserve(rowCount);
        for (uint32_t i = 0; i < rowCount; ++i)
        {
            auto keyLen = takeU32();
            auto keyBytes = take(keyLen);
            ReorgUndoRow row{executor_v1::StateKey{std::string(keyBytes)}, std::nullopt};
            auto const hasOld = takeU8();
            if (hasOld != 0 && hasOld != 1)
            {
                throw std::runtime_error("ReorgUndo: invalid hasOld flag");
            }
            if (hasOld == 1)
            {
                auto valueLen = takeU32();
                auto valueBytes = take(valueLen);
                storage::Entry entry;
                entry.set(std::string(valueBytes));
                row.oldValue.emplace(std::move(entry));
            }
            blob.rows.push_back(std::move(row));
        }
        if (remaining() != 0)
        {
            throw std::runtime_error("ReorgUndo: trailing bytes");
        }
        return blob;
    }
};

}  // namespace bcos::executor_v1::opstack
