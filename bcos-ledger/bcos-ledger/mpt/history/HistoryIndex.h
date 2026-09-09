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
#include <atomic>
#include <chrono>
#include <condition_variable>
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
/// offset of the record inside that row's payload. Sixteen bytes of RAM buy a query that reads
/// exactly one row and decodes exactly one record.
struct HistoryVersion
{
    protocol::BlockNumber block{};
    uint16_t shard{};
    uint32_t offset{};

    friend bool operator==(HistoryVersion const&, HistoryVersion const&) noexcept = default;
};

/// 8 + 2 + (2 padding) + 4. Pinned because the number is quoted as the per-version RAM cost of
/// retention (MPTHistory.h) and read off the field list gets it wrong — the padding after
/// `shard` is invisible there.
static_assert(sizeof(HistoryVersion) == 16, "HistoryVersion is the index's per-version cost");

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

/// How long a historical read waits for an open publish window before refusing.
///
/// The window is NOT short: it deliberately spans the block's `mergeBackStorage` (a RocksDB
/// write), the commit observer, the pending-result pop and a `getLedgerConfig` round trip, because
/// the publish has to be the last fallible step before the tip advances (HistoryCommit.h). So the
/// wait has to be bounded in TIME rather than in retries — a spin budget sized for "a few
/// microseconds" refuses healthy queries that merely overlapped an ordinary commit.
///
/// Two seconds is far longer than any commit on a healthy node and far shorter than an RPC
/// client's patience, so the timeout means "this node has a stuck committer", not "you were
/// unlucky". Tests override it (setPublishWindowWaitBudget) to keep the refusal path fast.
inline constexpr std::chrono::milliseconds kPublishWindowWaitBudget{2000};

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
            // Advance, not merely close: applyRetire may already have removed a block's versions
            // before the throw, so a reader straddling this publish has to see the counter move
            // even if no window was open. closePublishWindow would be a no-op in that case.
            bumpGenerationAfterPublish();
            throw;
        }
        bumpGenerationAfterPublish();
    }

    /// Announce that a commit has reached the point where DISK is ahead of this index.
    ///
    /// The window this opens is the one hole the two-phase commit leaves. A query resolves
    /// "unchanged since B" in two steps that cannot be one: `locate` says no version was recorded
    /// after B, and then the caller reads the CURRENT value from the committed plane. Between
    /// those two steps — and, worse, for the whole stretch between a block's merge and its
    /// publish — the disk already holds block N's new value while this index does not yet know N
    /// exists. A query for any B < N - 1 would pass admission, miss in the index, read the
    /// current value, and hand back block N's bytes labelled B. That is the fabricated answer G6
    /// forbids, and no amount of locking inside the index can see it, because the wrong value
    /// comes from a plane the index does not own.
    ///
    /// So the index publishes a COUNTER instead of trying to serialize the disk read. Even means
    /// quiescent; odd means a commit is somewhere between its merge and its publish. A reader
    /// samples it before `locate` and again after its current-value read: an odd sample, or two
    /// samples that differ, means a commit moved underneath the query and the "unchanged" reading
    /// is not established — so the query retries, and eventually refuses. It never silently
    /// answers.
    ///
    /// Relaxed ordering is enough on the reader side because the counter is not guarding data:
    /// every value the reader actually returns comes from the index under its own mutex or from
    /// the storage layer's own synchronisation. The counter only has to change, and a reader that
    /// sees a stale even value has by construction not yet been overtaken.
    ///
    /// Idempotent in the sense that matters: opening an already-open window is a programming
    /// error the commit path cannot make (one committer, RAII guard), and this checks rather than
    /// assumes.
    void openPublishWindow() noexcept
    {
        std::lock_guard lock(m_generationMutex);
        auto const current = m_generation.load(std::memory_order_relaxed);
        if ((current % 2) == 0)
        {
            m_generation.store(current + 1, std::memory_order_release);
        }
    }

    /// Close the window opened above. `publish` calls bumpGenerationAfterPublish instead, which
    /// subsumes this; the RAII guard on the commit path calls it on every failure path, so a
    /// commit that dies between merge and publish does not leave every later query waiting out
    /// the full budget.
    void closePublishWindow() noexcept
    {
        {
            std::lock_guard lock(m_generationMutex);
            auto const current = m_generation.load(std::memory_order_relaxed);
            if ((current % 2) == 0)
            {
                return;
            }
            m_generation.store(current + 1, std::memory_order_release);
        }
        m_generationChanged.notify_all();
    }

    /// The publish counter. Even: no commit is between its merge and its publish. Odd: one is.
    [[nodiscard]] uint64_t generation() const noexcept
    {
        return m_generation.load(std::memory_order_acquire);
    }

    /// Block until no commit is inside its publish window, or until @p budget runs out.
    ///
    /// @return true if the generation is even on return, false on timeout.
    ///
    /// Waiting rather than spinning is the point: the window spans a RocksDB write, so a reader
    /// that merely overlapped an ordinary commit must sleep through it and then answer, not burn
    /// a retry budget and refuse. The accepted cost is that an RPC (or scheduler) thread BLOCKS
    /// for the length of a commit — typically a few milliseconds — which is the same order as the
    /// storage read the query is about to do anyway, and far cheaper than a wrong answer or a
    /// spurious refusal.
    [[nodiscard]] bool waitForEvenGeneration(std::chrono::milliseconds budget) const
    {
        std::unique_lock lock(m_generationMutex);
        return m_generationChanged.wait_for(lock, budget,
            [this]() { return (m_generation.load(std::memory_order_acquire) % 2) == 0; });
    }

    /// How long waitForEvenGeneration is allowed to wait. kPublishWindowWaitBudget unless a test
    /// shortened it.
    [[nodiscard]] std::chrono::milliseconds publishWindowWaitBudget() const noexcept
    {
        return m_publishWindowWaitBudget;
    }

    /// Shorten (or lengthen) the wait. For tests that exercise the TIMEOUT path and would
    /// otherwise sit out the full production budget; production never calls it.
    void setPublishWindowWaitBudget(std::chrono::milliseconds budget) noexcept
    {
        m_publishWindowWaitBudget = budget;
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

    /// Leave the counter EVEN and STRICTLY GREATER than it was, whether or not a window was open.
    ///
    /// Closing an open window (+1) is the ordinary path. The other case matters just as much: a
    /// reader that sampled the counter before a publish and re-samples after its own read of the
    /// current value is relying on the two samples DIFFERING, and a publish that only closed a
    /// window it never had would leave them equal — the reader would then accept a current value
    /// it read across a commit. So a publish with no window open advances by two.
    void bumpGenerationAfterPublish() noexcept
    {
        {
            std::lock_guard lock(m_generationMutex);
            auto const current = m_generation.load(std::memory_order_relaxed);
            m_generation.store(current + ((current % 2) != 0 ? 1 : 2), std::memory_order_release);
        }
        m_generationChanged.notify_all();
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
    /// Even = quiescent, odd = a commit is between its merge and its publish. Outside m_mutex on
    /// purpose: readers sample it without taking the shared lock, and the commit path opens the
    /// window before it holds anything.
    std::atomic<uint64_t> m_generation{0};
    /// Guards only the counter's transitions and the wait below — never the maps. Always taken
    /// INSIDE m_mutex when both are held (publish), never the other way round.
    mutable std::mutex m_generationMutex;
    mutable std::condition_variable m_generationChanged;
    std::chrono::milliseconds m_publishWindowWaitBudget{kPublishWindowWaitBudget};
};

}  // namespace bcos::ledger::mpt::history
