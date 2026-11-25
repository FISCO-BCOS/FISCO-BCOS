// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "LedgerMethods.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <limits>
#include <span>
#include <stdexcept>

namespace bcos::ledger
{
struct BlockAnchor
{
    protocol::BlockNumber number = 0;
    h256 hash;
    bool operator==(BlockAnchor const&) const = default;
};

inline void encode(bytes& out, BlockAnchor const& block) noexcept
{
    codec::rlp::encode(out, static_cast<uint64_t>(block.number), block.hash);
}

inline size_t length(BlockAnchor const& block) noexcept
{
    return codec::rlp::length(static_cast<uint64_t>(block.number), block.hash);
}

inline void decode(bytesRef& in, BlockAnchor& block)
{
    uint64_t number = 0;
    codec::rlp::decode(in, number, block.hash);
    if (number > static_cast<uint64_t>(std::numeric_limits<protocol::BlockNumber>::max()))
    {
        throw std::runtime_error("Invalid engine cursor height");
    }
    block.number = static_cast<protocol::BlockNumber>(number);
}

// Genesis initializes all three cursors. An unset FCU field preserves the corresponding
// cursor unless the old safe block was removed or overtaken by finality.
struct PersistedEngineMeta
{
    BlockAnchor diskTip;
    std::optional<BlockAnchor> safe;
    std::optional<BlockAnchor> finalized;

    protocol::BlockNumber finalizedNumber() const { return finalized ? finalized->number : 0; }

    // A canonical redelivery is valid independently of this admission check.
    bool conflictsWithFinalized(protocol::BlockNumber number, h256 const& parentHash) const
    {
        return number <= finalizedNumber() ||
               (finalized && number - 1 == finalized->number && parentHash != finalized->hash);
    }

    bool operator==(PersistedEngineMeta const&) const = default;
};

inline void decode(bytesRef& in, PersistedEngineMeta& meta)
{
    std::vector<BlockAnchor> safe;
    std::vector<BlockAnchor> finalized;
    codec::rlp::decode(in, meta.diskTip, safe, finalized);
    if (safe.size() > 1 || finalized.size() > 1)
    {
        throw std::runtime_error("Invalid engine tag cardinality");
    }
    if (!safe.empty())
    {
        meta.safe = safe.front();
    }
    if (!finalized.empty())
    {
        meta.finalized = finalized.front();
    }
}

inline constexpr std::string_view ENGINE_META_KEY = "engine_meta";

inline storage::Entry engineMetaEntry(PersistedEngineMeta const& meta)
{
    bytes encoded;
    auto asList = [](std::optional<BlockAnchor> const& cursor) -> std::span<BlockAnchor const> {
        return cursor ? std::span{std::addressof(*cursor), size_t{1}} :
                        std::span<BlockAnchor const>{};
    };
    codec::rlp::encode(encoded, meta.diskTip, asList(meta.safe), asList(meta.finalized));
    storage::Entry entry;
    entry.set(std::move(encoded));
    return entry;
}

task::Task<void> writeEngineMeta(auto& storage, PersistedEngineMeta const& meta)
{
    co_await storage2::writeOne(
        storage, executor_v1::StateKey{SYS_CURRENT_STATE, ENGINE_META_KEY}, engineMetaEntry(meta));
}

task::Task<std::optional<PersistedEngineMeta>> readEngineMeta(auto& storage)
{
    auto entry = co_await storage2::readOne(
        storage, executor_v1::StateKeyView{SYS_CURRENT_STATE, ENGINE_META_KEY});
    if (!entry)
    {
        co_return std::nullopt;
    }
    PersistedEngineMeta meta;
    auto raw = entry->get();
    codec::rlp::decodeExact(
        bytesConstRef(reinterpret_cast<byte const*>(raw.data()), raw.size()), meta);
    co_return meta;
}

task::Task<PersistedEngineMeta> loadEngineMeta(auto& storage)
{
    auto const number = co_await getCurrentBlockNumber(storage, fromStorage);
    auto meta = co_await readEngineMeta(storage);
    if (!meta && number == 0)
    {
        if (auto hash = co_await readStoredBlockHash(storage, 0, /*requireExactSize=*/true))
        {
            BlockAnchor genesis{0, *hash};
            co_return PersistedEngineMeta{genesis, genesis, genesis};
        }
    }
    if (!meta || number < 0 || meta->diskTip.number != number || meta->finalizedNumber() > number ||
        (meta->safe &&
            (meta->safe->number > number || meta->safe->number < meta->finalizedNumber())))
    {
        throw std::runtime_error(
            "Missing or inconsistent engine cursors at ledger height " + std::to_string(number));
    }
    for (auto const& block : {std::optional(meta->diskTip), meta->safe, meta->finalized})
    {
        if (block)
        {
            auto hash =
                co_await readStoredBlockHash(storage, block->number, /*requireExactSize=*/true);
            if (!hash || *hash != block->hash)
            {
                throw std::runtime_error("Engine cursor does not match canonical hash at height " +
                                         std::to_string(block->number));
            }
        }
    }
    co_return *meta;
}
}  // namespace bcos::ledger
