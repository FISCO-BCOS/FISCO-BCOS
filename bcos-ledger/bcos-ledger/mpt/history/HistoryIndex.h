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
 * @file HistoryIndex.h
 * @brief The in-memory (key -> ordered versions) index that turns a history query into one shard
 *        read (layout spec §1.3)
 */
#pragma once

#include "../Errors.h"
#include "HistoryErrors.h"
#include "HistoryRowCodec.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// Where one key's pre-image for one block physically is: the block's shard row, and the byte
/// offset of the record inside that row's payload. Twelve bytes of RAM buy a query that reads
/// exactly one row and decodes exactly one record.
struct HistoryVersion
{
    protocol::BlockNumber block{};
    uint16_t shard{};
    uint32_t offset{};

    friend bool operator==(HistoryVersion const&, HistoryVersion const&) noexcept = default;
};

/// What one `put` computed but has NOT published: the rows are in the block's WriteBatch, and the
/// index must not learn about them until that batch lands (G9). The commit path holds this
/// between `stageBlockHistory` and `publishBlockHistory`, and drops it on a merge failure.
///
/// Declared here rather than in ReverseHistoryStore.h because HistoryIndex::publish takes it; the
/// names and fields are the ones layout spec §1.4 pins.
struct StagedBlock
{
    protocol::BlockNumber block{};
    BlockMeta meta;
    std::vector<std::pair<bcos::bytes, HistoryVersion>> versions;
};

/// What one `expire` deleted, so the index can drop the same block without re-reading the shards.
struct RetiredBlock
{
    protocol::BlockNumber block{};
    std::vector<bcos::bytes> keys;
    std::size_t shardsDeleted{};
};

/// Whether the index is entitled to answer at all (G10).
enum class IndexState : uint8_t
{
    /// Never rebuilt and never published to. NOT the same as "this chain has no history": an
    /// index that has not been built cannot tell the two apart, so it refuses.
    Empty,
    /// A rebuild completed, or a block was published. Queries are served.
    Ready,
    /// A rebuild or a publish failed. Whatever is in the maps may be missing versions, and a
    /// missing version reads as "the key never changed" — the silent wrong answer this component
    /// exists to prevent. Every query refuses until a successful rebuild replaces it.
    Unavailable,
};

/// Transparent hash so a query can look up by `std::string_view` without allocating a key.
struct TransparentStringHash
{
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

/// The query index: for every key with retained history, the blocks that changed it, in ascending
/// order, each paired with where its pre-image sits on disk.
///
/// Pure memory — it performs no I/O and knows nothing about storage. It is DERIVED data: every
/// entry can be recomputed by walking the shard rows (ReverseHistoryStore::rebuild), which is why
/// it is safe to lose it on a crash and why it is never written to disk. That is the whole saving
/// of the optimized layout: no index rows to write on commit, no point-deletes to issue on expiry.
///
/// It carries its own `std::shared_mutex`: the commit path publishes under a unique lock while
/// RPC and the scheduler read under shared locks.
class HistoryIndex
{
public:
    HistoryIndex() = default;
    // The shared_mutex makes the object neither copyable nor movable. `replace` is how a rebuilt
    // index is installed, and it moves the CONTENTS rather than the object.
    HistoryIndex(const HistoryIndex&) = delete;
    HistoryIndex(HistoryIndex&&) = delete;
    HistoryIndex& operator=(const HistoryIndex&) = delete;
    HistoryIndex& operator=(HistoryIndex&&) = delete;
    ~HistoryIndex() = default;

    /// Add block @p recorded, drop block @p retired, and move the boundary to @p boundaryWritten.
    ///
    /// Called ONLY after the block's WriteBatch has landed (G9). Everything it does is under one
    /// unique lock, so a reader never sees a block half-added or half-retired.
    ///
    /// @p recorded must name a block strictly above every block this index already holds — see
    /// applyStaged for why the whole lookup depends on it.
    ///
    /// If anything here throws — an allocation failure, or that ordering check — the maps may be
    /// partially mutated, and a partially mutated index answers "this key never changed" for the
    /// versions it is missing. So the state goes to Unavailable and the exception is rethrown:
    /// refusing is the only safe reading of a half-applied publish.
    void publish(StagedBlock&& recorded, std::optional<RetiredBlock> retired,
        std::optional<protocol::BlockNumber> boundaryWritten)
    {
        std::unique_lock lock(m_mutex);
        try
        {
            if (retired)
            {
                applyRetire(*retired);
            }
            applyStaged(std::move(recorded));
            applyBoundary(boundaryWritten);
            m_state = IndexState::Ready;
        }
        catch (...)
        {
            m_state = IndexState::Unavailable;
            throw;
        }
    }

    /// Install a freshly rebuilt index in place of this one.
    ///
    /// The state becomes Ready unconditionally, and that is the point rather than an oversight: a
    /// rebuild that hit any inconsistency threw instead of returning, so reaching `replace` IS the
    /// proof that the shards were walked end to end. A rebuild that legitimately found nothing —
    /// a chain that has recorded no history yet — therefore installs an empty Ready index, which
    /// answers "no recorded change" rather than refusing, and that is correct: the walk saw the
    /// whole retained range.
    void replace(HistoryIndex&& rebuilt)
    {
        std::unique_lock lock(m_mutex);
        std::unique_lock rebuiltLock(rebuilt.m_mutex);
        m_versions = std::move(rebuilt.m_versions);
        m_blocks = std::move(rebuilt.m_blocks);
        m_boundary = rebuilt.m_boundary;
        m_versionCount = rebuilt.m_versionCount;
        m_state = IndexState::Ready;
    }

    /// Refuse every query from here on. The startup path calls it after a failed rebuild; publish
    /// latches the same state itself, under the lock it already holds.
    void markUnavailable()
    {
        std::unique_lock lock(m_mutex);
        m_state = IndexState::Unavailable;
    }

    /// Seed the boundary a rebuild read off disk, before it walks the shards.
    ///
    /// Separate from `publish` because the boundary must survive a rebuild that finds ZERO blocks:
    /// a store whose whole retained window has been expired still has to refuse queries below the
    /// boundary, and there is no block to hang that fact on. Max, not assignment — the boundary
    /// only ever grows.
    void setBoundary(std::optional<protocol::BlockNumber> boundary)
    {
        std::unique_lock lock(m_mutex);
        applyBoundary(boundary);
    }

    /// The first recorded change strictly AFTER @p block, which is the version whose pre-image IS
    /// the value at @p block. Nullopt means no change was recorded after @p block, so the caller
    /// may read the current value — and that reading is only sound because `locate` has already
    /// established that this index is Ready and covers @p block (G10).
    [[nodiscard]] std::optional<HistoryVersion> firstChangeAfter(
        std::span<const bcos::byte> key, protocol::BlockNumber block) const
    {
        std::shared_lock lock(m_mutex);
        return firstChangeAfterLocked(asStringView(key), block);
    }

    /// The query path's three steps under ONE shared lock: the state check, the boundary check and
    /// the lookup (layout spec §1.4, readAt rule 2). Taking three separate shared locks would let
    /// a publish or a rebuild land between them and answer from a mix of two index generations.
    ///
    /// @throws HistoryIndexUnavailable when the index is not Ready.
    /// @throws HistoryPruned when @p block is below the retention boundary.
    [[nodiscard]] std::optional<HistoryVersion> locate(
        std::span<const bcos::byte> key, protocol::BlockNumber block) const
    {
        std::shared_lock lock(m_mutex);
        if (m_state != IndexState::Ready)
        {
            BOOST_THROW_EXCEPTION(
                HistoryIndexUnavailable() << bcos::errinfo_comment(
                    "the reverse-history query index has not been rebuilt, or a rebuild or "
                    "publish left it unusable"));
        }
        if (m_boundary && *m_boundary > block)
        {
            BOOST_THROW_EXCEPTION(HistoryPruned() << bcos::errinfo_comment(
                                      "the requested block is below the retention boundary this "
                                      "store recorded on disk"));
        }
        return firstChangeAfterLocked(asStringView(key), block);
    }

    /// Was block @p block's history recorded here? One map probe, no I/O.
    [[nodiscard]] bool hasBlock(protocol::BlockNumber block) const
    {
        std::shared_lock lock(m_mutex);
        return m_blocks.contains(block);
    }

    /// The block meta the Meta row carried, for a block this index holds.
    [[nodiscard]] std::optional<BlockMeta> blockMeta(protocol::BlockNumber block) const
    {
        std::shared_lock lock(m_mutex);
        auto iterator = m_blocks.find(block);
        if (iterator == m_blocks.end())
        {
            return std::nullopt;
        }
        return iterator->second;
    }

    /// The oldest block this store can still answer for, as the index knows it.
    [[nodiscard]] std::optional<protocol::BlockNumber> boundary() const
    {
        std::shared_lock lock(m_mutex);
        return m_boundary;
    }

    [[nodiscard]] IndexState state() const
    {
        std::shared_lock lock(m_mutex);
        return m_state;
    }

    /// Keys with at least one retained version.
    [[nodiscard]] std::size_t keyCount() const
    {
        std::shared_lock lock(m_mutex);
        return m_versions.size();
    }

    /// Versions across all keys — the sum of every retained block's recordCount, which is what a
    /// rebuild test compares against the meta rows.
    [[nodiscard]] std::size_t versionCount() const
    {
        std::shared_lock lock(m_mutex);
        return m_versionCount;
    }

    /// Blocks whose history this index holds.
    [[nodiscard]] std::size_t blockCount() const
    {
        std::shared_lock lock(m_mutex);
        return m_blocks.size();
    }

private:
    /// The lookup itself. `upper_bound` on an ascending vector: the first version whose block is
    /// strictly greater than @p block.
    [[nodiscard]] std::optional<HistoryVersion> firstChangeAfterLocked(
        std::string_view key, protocol::BlockNumber block) const
    {
        auto iterator = m_versions.find(key);
        if (iterator == m_versions.end())
        {
            return std::nullopt;
        }
        auto const& versions = iterator->second;
        auto position = std::upper_bound(versions.begin(), versions.end(), block,
            [](protocol::BlockNumber bound, HistoryVersion const& version) {
                return bound < version.block;
            });
        if (position == versions.end())
        {
            return std::nullopt;
        }
        return *position;
    }

    /// Append one published block.
    ///
    /// Blocks must arrive in STRICTLY ASCENDING order, and that is the precondition the whole
    /// index rests on: versions are appended with push_back, so a key's vector is sorted only
    /// because the block numbers were. Publish a block below the current maximum and the vector
    /// stops being sorted, at which point `upper_bound` in firstChangeAfterLocked has undefined
    /// behaviour and hands back a plausible wrong version — the one silently-wrong answer this
    /// component exists to prevent, reached from inside. Re-publishing the SAME block is the
    /// special case of that (it would also give one key two versions for one block, and retiring
    /// the block would leave one behind), so one check covers both.
    ///
    /// The check comes FIRST, before any version vector has been touched, so a refused publish
    /// leaves the index exactly as it was rather than half-applied.
    void applyStaged(StagedBlock&& recorded)
    {
        if (!m_blocks.empty() && m_blocks.rbegin()->first >= recorded.block)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "history index blocks must be published in strictly ascending order"));
        }
        m_blocks.emplace(recorded.block, recorded.meta);
        for (auto& [key, version] : recorded.versions)
        {
            auto const keyView = asStringView(key);
            auto iterator = m_versions.find(keyView);
            if (iterator == m_versions.end())
            {
                iterator =
                    m_versions.emplace(std::string(keyView), std::vector<HistoryVersion>{}).first;
            }
            iterator->second.push_back(version);
            ++m_versionCount;
        }
    }

    /// Drop one expired block. Its versions sit at the FRONT of each key's vector — it is the
    /// oldest block still retained — so this is a bounded erase, not a scan.
    void applyRetire(RetiredBlock const& retired)
    {
        for (auto const& key : retired.keys)
        {
            auto iterator = m_versions.find(asStringView(key));
            if (iterator == m_versions.end())
            {
                continue;
            }
            auto& versions = iterator->second;
            auto const removed = std::erase_if(versions,
                [&](HistoryVersion const& version) { return version.block == retired.block; });
            m_versionCount -= removed;
            if (versions.empty())
            {
                m_versions.erase(iterator);
            }
        }
        m_blocks.erase(retired.block);
    }

    /// The boundary only ever grows: expired data does not come back, and the block an expiry
    /// names is not monotonic across callers (raising the retention depth makes N - H jump
    /// backwards). Assigning would claim heights are intact whose shards an earlier, higher
    /// expiry already deleted.
    void applyBoundary(std::optional<protocol::BlockNumber> boundary)
    {
        if (boundary && (!m_boundary || *m_boundary < *boundary))
        {
            m_boundary = *boundary;
        }
    }

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::vector<HistoryVersion>, TransparentStringHash,
        std::equal_to<>>
        m_versions;
    std::map<protocol::BlockNumber, BlockMeta> m_blocks;
    std::optional<protocol::BlockNumber> m_boundary;
    std::size_t m_versionCount{};
    IndexState m_state{IndexState::Empty};
};

}  // namespace bcos::ledger::mpt::history
