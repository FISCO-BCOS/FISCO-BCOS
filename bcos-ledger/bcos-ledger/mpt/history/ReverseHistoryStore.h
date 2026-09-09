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
 * @file ReverseHistoryStore.h
 * @brief One block's pre-images packed into per-block shards on disk, located by an in-memory
 *        index that is rebuilt from those shards at startup (layout spec §1.1-§1.5)
 *
 * The earlier layout stored each pre-image twice — once in a `(key, block)` index row that
 * answered point queries, once in a `(block, shard)` manifest that listed the block's keys. This
 * one stores it once, in the shard, and keeps the `(key, block)` order in RAM. Three consequences
 * drive the whole file:
 *
 *  - a commit writes `shardCount + 1` rows instead of `keyCount + shardCount`;
 *  - an expiry deletes `shardCount + 1` rows instead of one per key plus the shards;
 *  - the index is derived, so it must be REBUILT at startup and must refuse to answer until it
 *    has been (G10) — "not in the index" is never allowed to mean "the key never changed".
 */
#pragma once

#include "../Errors.h"
#include "HistoryErrors.h"
#include "HistoryIndex.h"
#include "HistoryRowCodec.h"
#include "HistoryTables.h"
#include "ShardTableWalk.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// A storage the component writes history rows and expiry deletions into. In production this is
/// the block's mutable layer, so those rows ride the block's single WriteBatch (G3).
template <class Storage>
concept WritableStateStorage = requires(Storage& storage,
    std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> keyValues,
    std::vector<executor_v1::StateKey> keys) {
    { storage2::writeSome(storage, keyValues) } -> task::IsAwaitable;
    { storage2::removeSome(storage, keys) } -> task::IsAwaitable;
};

/// A storage a point read runs against — one key in, one value out. This is all `readAt` needs:
/// the version is located in memory, so the disk work is a single `readOne` of a single shard
/// row. The retention-boundary read is a `readOne` too.
template <class Storage>
concept ReadableStateStorage = requires(Storage& storage, executor_v1::StateKey key) {
    { storage2::readOne(storage, key) } -> task::IsAwaitable;
};

/// A storage a whole-block WALK runs against: `expire` reads a block's rows before deleting them
/// and the rebuild walks the shard table end to end, and both position an iterator and go
/// forward. Point reads come with it because `expire` also reads the boundary row.
///
/// Kept separate from ReadableStateStorage because the two are genuinely different requirements
/// on a caller: a query plane only has to answer point reads, and demanding a seek of it would
/// exclude storages that can serve every query this component makes.
template <class Storage>
concept QueryableStateStorage = SeekableStateStorage<Storage> && ReadableStateStorage<Storage>;

/// One key changed by one block, paired with the value it held when the block began.
/// Both fields are non-owning views into the caller's diff — put() copies out of them.
struct HistoryEntry
{
    std::span<const bcos::byte> key;
    /// nullopt when the key did not exist before this block (written as tag 0x00).
    std::optional<std::span<const bcos::byte>> oldValue;
};

/// readAt outcome: the key did not exist at the queried block (the record's tag is 0x00).
struct HistoryAbsent
{
    friend bool operator==(HistoryAbsent, HistoryAbsent) noexcept { return true; }
};

/// readAt outcome: no change was recorded after the queried block, so the key has not changed
/// since — the current value IS the historical one and the caller should read it from the live
/// plane (spec B.3). Only trustworthy because the index was Ready and its boundary covers the
/// block; without both, "not in the index" is silence, not evidence (G6, G10).
struct HistoryUseCurrent
{
    /// The index's publish generation as it stood BEFORE the lookup that produced this answer,
    /// and always even — readAt refuses to conclude "unchanged" while a commit is mid-publish.
    ///
    /// The caller must read the current value and then check this against `index().generation()`
    /// again: equal means no commit moved underneath the query and the current value really is
    /// block B's; different means the disk it just read may already be ahead of the index, and
    /// the answer has to be recomputed or refused. HistoryRead.h::readAtOrCurrent is the one
    /// place that does this, and every caller goes through it.
    uint64_t generation{};

    /// Equality IGNORES the generation, on purpose: it is provenance for the caller's own
    /// re-check, not part of the answer. Two "this key has not changed since B" outcomes are the
    /// same outcome whichever index generation produced them — and a test comparing answers
    /// across two indexes (the rebuild cases) would otherwise be comparing counters.
    friend bool operator==(HistoryUseCurrent const&, HistoryUseCurrent const&) noexcept
    {
        return true;
    }
};

/// readAt outcome, third case: the key's value at the queried block, as `bcos::bytes`.
using ReadAtResult = std::variant<bcos::bytes, HistoryAbsent, HistoryUseCurrent>;

/// What expire() does to the retention boundary — the row recording the OLDEST block this store
/// can still answer for (HistoryTables.h).
///
/// The parameter exists because "discard block E's history" has two callers that move in opposite
/// directions along the chain, and only one of them changes which block is the oldest.
enum class RetentionBoundary : uint8_t
{
    /// Leave the boundary alone. The caller is discarding history from the TOP — an operational
    /// rollback walks tip downwards, reverse-applying block N from N's own records and then
    /// dropping them. Which block is the OLDEST answerable does not change when the newest one
    /// goes, so the boundary must not move; and because it only ever grows, advancing it once per
    /// block would ratchet it all the way to the pre-rollback tip, leaving the store refusing
    /// every historical read below that height while the shards that answer them are still there.
    Keep,
    /// Advance the boundary to the expired block. The caller is the commit path, where E = N - H
    /// is the block LEAVING the window from the bottom, so everything below E is now gone. The
    /// write rides the same batch as the deletes.
    Advance,
};

/// One block's whole recorded diff, as readBlock hands it back: the meta row plus every record in
/// shard order. Rollback and the B.10 audits both need this to be COMPLETE, which is why readBlock
/// verifies the counts the meta row declares.
struct BlockHistory
{
    BlockMeta meta;
    std::vector<std::pair<bcos::bytes, std::optional<bcos::bytes>>> records;
};

/// What one expire() pass did.
struct ExpireReport
{
    /// Records the block's shards listed. Zero also means "already expired" — expire is idempotent.
    std::size_t keyCount{};
    /// Shard rows found live and deleted. The meta row goes with them, so the deletes issued are
    /// `shardsDeleted + 1` when the meta row was live too.
    std::size_t shardsDeleted{};
    /// The block as the in-memory index must now forget it — nullopt when the pass found nothing
    /// live, i.e. the block was never recorded or was already expired. Handed to publish().
    std::optional<RetiredBlock> retired;
};

/// What one rebuild() pass walked.
struct RebuildReport
{
    std::size_t blocks{};
    std::size_t records{};
    std::size_t bytesScanned{};
};

/// The reverse history of one key space: every block's pre-images packed into that block's shard
/// rows, plus the in-memory index that says which shard and which byte offset answers a query.
///
/// Instantiated twice over the same code — `StateHistoryStore` for state rows, `TrieHistoryStore`
/// for path-addressed trie nodes (spec B.8). @p Tables selects which pair of state tables the
/// instance owns; everything else is identical, which is the point.
///
/// Unlike the earlier all-static store this one is an OBJECT: it owns the index, so a node holds
/// exactly one instance per key space (PR-C's `MPTHistory`) and every reader shares it. Disk still
/// arrives as a parameter — in production the block's mutable layer for writes and the committed
/// backend for reads.
template <HistoryTables const& Tables>
class ReverseHistoryStore
{
public:
    ReverseHistoryStore() = default;
    // The index carries a shared_mutex, so neither it nor this wrapper can be copied or moved.
    ReverseHistoryStore(const ReverseHistoryStore&) = delete;
    ReverseHistoryStore(ReverseHistoryStore&&) = delete;
    ReverseHistoryStore& operator=(const ReverseHistoryStore&) = delete;
    ReverseHistoryStore& operator=(ReverseHistoryStore&&) = delete;
    ~ReverseHistoryStore() = default;

    /// Record @p entries as the pre-images of block @p block: the shard rows that hold them, plus
    /// the one meta row that says how many of each to expect.
    ///
    /// Pure append: every row is a fresh Put and nothing is read first, so write amplification is
    /// the size of this block's diff and does not grow with the retention depth (spec B.4). All
    /// rows go out in ONE writeSome, which in production is one contribution to the block's single
    /// WriteBatch (G3).
    ///
    /// It does NOT touch the index. The returned StagedBlock is the index update this write
    /// implies, and the caller applies it with publish() only after the batch has landed (G9); if
    /// the merge fails, dropping the StagedBlock leaves the index exactly as it was.
    ///
    /// @param entries one record per key the block changed, each carrying the value the key held
    ///        when the block BEGAN. A key must appear at most once: a second record for the same
    ///        (key, block) would give the key two index versions for one block, and every later
    ///        query would resolve to whichever came second — so a duplicate throws rather than
    ///        being ignored. The caller does the intra-block deduplication (spec B.4).
    /// @param shardByteCap the payload size at which a new shard starts. A key whose own record
    ///        exceeds the cap still gets a shard — the cap bounds row size, it cannot split a
    ///        record.
    ///
    /// A block that changed nothing still gets `Meta{shardCount = 1, recordCount = 0}` and an
    /// empty shard 0 (spec B.10 ②): the audit reads the window as one meta row per block and
    /// treats a gap as fatal, so "no changes" must be recorded as such rather than being
    /// indistinguishable from a lost block.
    template <WritableStateStorage Storage>
    task::Task<StagedBlock> put(Storage& batch, protocol::BlockNumber block,
        bcos::h256 const& blockHash, std::span<HistoryEntry const> entries,
        std::size_t shardByteCap) const
    {
        if (shardByteCap == 0)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                      "history shard byte cap must be positive"));
        }
        // Checked HERE rather than left to the codec: metaRowKey/shardRowKey do refuse a negative
        // block, but only after every record has been encoded and every version staged, so the
        // failure would arrive with a batch already half-built and a message about a row key
        // rather than about the argument that was wrong.
        if (block < 0)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation()
                                  << bcos::errinfo_comment("history block number must not be "
                                                           "negative")
                                  << errinfo_historyBlock(block));
        }

        StagedBlock staged{.block = block, .meta = {}, .versions = {}};
        staged.versions.reserve(entries.size());

        std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> rows;
        std::unordered_set<std::string_view> seenKeys;
        seenKeys.reserve(entries.size());

        std::string shardPayload;
        std::size_t shardIndex = 0;
        auto flushShard = [&]() {
            rows.emplace_back(executor_v1::StateKey{Tables.shard, shardRowKey(block, shardIndex)},
                executor_v1::StateValue{std::move(shardPayload)});
            shardPayload.clear();
            ++shardIndex;
        };

        for (auto const& entry : entries)
        {
            if (!seenKeys.insert(asStringView(entry.key)).second)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation() << bcos::errinfo_comment(
                        "the same key appears twice in one block's history entries; only the "
                        "block-start value may be recorded"));
            }

            auto const size = recordSize(entry.key, entry.oldValue);
            if (!shardPayload.empty() && shardPayload.size() + size > shardByteCap)
            {
                flushShard();
            }
            // shardRowKey enforces the 2-byte shard field, but the ordinal is narrowed into the
            // index version BEFORE the row key is built, so it is checked here too.
            if (shardIndex > kMaxShardIndex)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                          "history shard ordinal overflows the 2-byte field"));
            }
            auto const offset = appendRecord(shardPayload, entry.key, entry.oldValue);
            if (offset > std::numeric_limits<uint32_t>::max())
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                          "history record offset overflows its 4-byte field"));
            }
            staged.versions.emplace_back(bcos::bytes(entry.key.begin(), entry.key.end()),
                HistoryVersion{.block = block,
                    .shard = static_cast<uint16_t>(shardIndex),
                    .offset = static_cast<uint32_t>(offset)});
        }
        flushShard();

        staged.meta = BlockMeta{.shardCount = static_cast<uint32_t>(shardIndex),
            .recordCount = static_cast<uint32_t>(entries.size()),
            .blockHash = blockHash};
        // Appended last because shardCount is only known once the loop is done; the batch applies
        // the rows as a set, so position carries no meaning on disk.
        rows.emplace_back(executor_v1::StateKey{Tables.shard, metaRowKey(block)},
            executor_v1::StateValue{metaRowValue(
                staged.meta.shardCount, staged.meta.recordCount, staged.meta.blockHash)});

        co_await storage2::writeSome(batch, std::move(rows));
        co_return staged;
    }

    /// Apply to the index what @p recorded, @p retired and @p boundaryWritten already did to disk.
    ///
    /// G9: the commit path calls this ONLY after the block's WriteBatch has landed. A merge that
    /// throws leaves the StagedBlock unpublished, and the index therefore never learns about rows
    /// that are not there — the next query answers exactly as it did before the failed block.
    void publish(StagedBlock&& recorded, std::optional<RetiredBlock> retired,
        std::optional<protocol::BlockNumber> boundaryWritten)
    {
        m_index.publish(std::move(recorded), std::move(retired), boundaryWritten);
    }

    /// The index this store answers from. Read-only: it is mutated through publish, rebuild and
    /// markUnavailable.
    [[nodiscard]] HistoryIndex const& index() const { return m_index; }

    /// Refuse every history query from here on. The startup path calls this when rebuild() throws
    /// (layout spec §1.5): the node keeps running and keeps committing blocks, and only the
    /// history reads are refused, which is the difference between a degraded node and a dead one.
    void markUnavailable() { m_index.markUnavailable(); }

    /// The publish window a commit holds between its merge and its publish (HistoryIndex.h).
    /// Opened by HistoryCommit.h's RAII guard immediately before the merge and closed by the
    /// publish or by that guard; readers observe it through `index().generation()`.
    void openPublishWindow() noexcept { m_index.openPublishWindow(); }
    void closePublishWindow() noexcept { m_index.closePublishWindow(); }
    [[nodiscard]] uint64_t generation() const noexcept { return m_index.generation(); }
    /// Tests only; production leaves it at kPublishWindowWaitBudget (HistoryIndex.h).
    void setPublishWindowWaitBudget(std::chrono::milliseconds budget) noexcept
    {
        m_index.setPublishWindowWaitBudget(budget);
    }

    /// The value @p key held at block @p block.
    ///
    /// The answer is the OLD value recorded by the first change after @p block: between @p block
    /// and that change the key was untouched, so that old value is exactly the block-@p block
    /// value (spec §0.4). One in-memory lookup, one row read, one record decoded, independent of
    /// how far back @p block is.
    ///
    /// Four hard rules, in order (layout spec §1.4):
    ///
    ///  1. the window guard runs BEFORE anything else (G5);
    ///  2. the state check, the boundary check and the lookup happen under ONE shared lock;
    ///  3. a located version whose shard row is gone throws HistoryPruned — it never falls back to
    ///     the current value;
    ///  4. an "unchanged since B" answer is only reached while the index's publish generation is
    ///     EVEN, and it reports the generation it saw so the caller can prove no commit moved
    ///     underneath the current-value read that follows.
    ///
    /// @param tip the chain's current block number.
    /// @param depth the retention window, i.e. H_state or H_proof for this instance.
    /// @throws HistoryPruned when @p block predates the retained window or the located shard is
    ///         gone.
    /// @throws HistoryIndexUnavailable when the index has not been rebuilt or is unusable, or
    ///         when a commit stayed mid-publish for the whole retry budget below.
    /// @throws InvalidHistoryBlock when @p block is negative, when @p block is ahead of @p tip,
    ///         or when @p depth is negative.
    template <ReadableStateStorage Storage>
    task::Task<ReadAtResult> readAt(Storage& backend, std::span<const bcos::byte> key,
        protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth) const
    {
        // The window guard runs BEFORE the lookup, and that order is the whole point (spec B.3,
        // G5): a lookup that finds nothing cannot tell "never changed after B, so the current
        // value is the answer" from "the record was expired away, so the current value is a wrong
        // answer". Only the window bound separates them. HISTORY_GUARD_DISABLED exists solely so
        // the test suite can compile a build without the guard and demonstrate that the
        // out-of-window case then returns a plausible-looking wrong value; nothing defines it.
#ifndef HISTORY_GUARD_DISABLED
        checkWindow(block, tip, depth);
#endif

        // The publish-window gate, and it runs BEFORE `locate` for two reasons.
        //
        // One: a "nothing recorded after B" answer sends the caller to the CURRENT value, and that
        // is only sound while the index is not behind the disk. Between a block's merge and its
        // publish it IS behind — the disk already holds block N, the index does not know N — so a
        // miss there would hand back N's bytes labelled B.
        //
        // Two: the same batch also carries the expiry of the block leaving the window, so inside
        // it the shard rows the index still names may ALREADY be deleted. Locating first would
        // find a version and then fail its row read as HistoryPruned — a refusal, but for the
        // wrong reason and at a height the node can in fact still answer once the publish lands.
        //
        // Waiting rather than spinning: the window spans the block's RocksDB write, the commit
        // observer and a getLedgerConfig round trip, because the publish must be the LAST fallible
        // step before the tip advances (HistoryCommit.h). Milliseconds, not microseconds. A reader
        // that merely overlapped an ordinary commit therefore SLEEPS through it and then answers;
        // only a committer stuck for longer than kPublishWindowWaitBudget produces a refusal, and
        // that refusal says "retry", not "restart the node".
        auto const deadline = std::chrono::steady_clock::now() + m_index.publishWindowWaitBudget();
        std::optional<HistoryVersion> located;
        uint64_t generation = 0;
        for (;;)
        {
            generation = m_index.generation();
            if ((generation % 2) == 0)
            {
                // Throws when the index is not entitled to answer, so everything below this line
                // is reasoning about a Ready index whose boundary covers @p block (G10).
                located = m_index.locate(key, block);
                // A miss is only trustworthy if no publish started while we were looking it up.
                if (located || m_index.generation() == generation)
                {
                    break;
                }
            }
            auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero() ||
                !m_index.waitForEvenGeneration(remaining))
            {
                BOOST_THROW_EXCEPTION(
                    HistoryIndexUnavailable()
                    << bcos::errinfo_comment(std::string("a commit's publish window on the ")
                               .append(Tables.shard)
                               .append(" history stayed open longer than the wait budget; the "
                                       "query was not answered and should be retried")));
            }
        }

        if (block >= tip || !located)
        {
            // At or above the tip no later block can have recorded a pre-image, and below it an
            // empty lookup means the key has not changed since — both are the current value.
            //
            // The tip arm is checked AFTER locate, not before, so even a query that needs no
            // history at all still requires an available index. Same reading as
            // HistoryRead.h::historyCoversBlock: a node whose index is unusable does not know
            // what it holds, and a tip answer from it would look like a healthy node's.
            //
            // The generation rides along: the caller has still to READ the current value, and a
            // commit landing between here and that read would make it wrong. Only
            // HistoryRead.h::readAtOrCurrent may act on this outcome.
            co_return HistoryUseCurrent{.generation = generation};
        }

        auto row = co_await storage2::readOne(backend,
            executor_v1::StateKey{Tables.shard, shardRowKey(located->block, located->shard)});
        if (!row)
        {
            // The index located a version and the shard is not there — an expiry landed between
            // the lookup and this read, or on a logical-deletion layer the row is a sentinel
            // (readOne reports both as absent). Falling through to the current value here is the
            // silent wrong answer the whole layout exists to prevent (G6, G10).
            BOOST_THROW_EXCEPTION(
                HistoryPruned() << bcos::errinfo_comment(
                    "the shard holding the requested pre-image was deleted after the index "
                    "located it"));
        }
        auto const record = decodeRecordAt(row->get(), located->offset);
        if (record.key != asStringView(key))
        {
            // The offset the index held no longer names this key's record: the index and the
            // shard disagree about the layout of the same bytes. Returning the record found there
            // would answer with another key's value.
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "the history record at the indexed offset belongs to a different key"));
        }
        if (!record.oldValue)
        {
            co_return HistoryAbsent{};
        }
        co_return bcos::bytes(record.oldValue->begin(), record.oldValue->end());
    }

    /// Was block @p block's history ever RECORDED here? Pure memory — the index holds one entry
    /// per retained block, put there by publish or by the rebuild walk.
    ///
    /// This answers a question the window guard cannot: readAt's window bound says "block B is
    /// young enough not to have been expired", which is a statement about the RETENTION PARAMETER,
    /// not about what was ever written. A node whose history began after block B — the depth was
    /// raised, the MPT was enabled mid-life, the era predates this feature — has blocks inside its
    /// nominal window that were never recorded, and for those every key misses the index and
    /// reports HistoryUseCurrent: today's value, presented as block B's. Probing the record itself
    /// is what turns that into a refusal (G6).
    [[nodiscard]] bool recordedBlock(protocol::BlockNumber block) const
    {
        return m_index.hasBlock(block);
    }

    /// Block @p block's whole recorded diff: the meta row plus every record, in shard order. Used
    /// by rollback and by the B.10 audits, both of which need the list to be COMPLETE — so the
    /// counts the meta row declares are verified against what the walk actually read.
    ///
    /// @throws MPTInvariantViolation when the meta row is missing, when the shard count or the
    ///         record count disagrees with it, or when a row is present but carries a deletion
    ///         sentinel rather than bytes. That last shape is what an expiry looks like on the
    ///         mutable layer, whose removeSome marks rather than erases (MemoryStorage
    ///         LOGICAL_DELETION, the mode GlobalStateMutableStorage runs in): the records such a
    ///         shard held are unreadable, and a caller that needs the complete diff must not be
    ///         handed a short one (G6). expire() takes the opposite reading of the same row.
    template <SeekableStateStorage Storage>
    task::Task<BlockHistory> readBlock(Storage& backend, protocol::BlockNumber block) const
    {
        BlockHistory history;
        auto const scan = co_await scanBlock(backend, block, DeletedShardPolicy::Reject,
            [&](ShardRecord const& record, HistoryVersion const&) {
                std::optional<bcos::bytes> oldValue;
                if (record.oldValue)
                {
                    oldValue.emplace(record.oldValue->begin(), record.oldValue->end());
                }
                history.records.emplace_back(
                    bcos::bytes(record.key.begin(), record.key.end()), std::move(oldValue));
            });

        if (!scan.metaFound)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                      "history block has no meta row; its diff cannot be read"));
        }
        if (scan.shardsRead != scan.meta.shardCount)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "history block holds a different number of shards than its meta row declares"));
        }
        if (scan.recordsRead != scan.meta.recordCount)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "history block holds a different number of records than its meta row "
                    "declares"));
        }
        history.meta = scan.meta;
        co_return history;
    }

    /// Drop block @p block from the retained window: delete its shard rows and its meta row.
    ///
    /// This is the saving the layout was reshaped for. The old layout had to delete one index row
    /// per key, so an expiry cost as much as the commit that created it; here the pre-images live
    /// inside the shards, so `shardCount + 1` removes take the whole block — and the in-memory
    /// versions go with them, without a single point-delete.
    ///
    /// Deletions are written to @p batch, so in the default mode they land in the committing
    /// block's WriteBatch and the whole expiry is atomic with the commit (spec B.5).
    ///
    /// Idempotent on every storage, including the one G3 puts it on. The production mutable layer
    /// is MemoryStorage with LOGICAL_DELETION (GlobalStateStorageInitializer.h:15-19), where a
    /// removed row stays in the container as a deletion sentinel and the iterator still yields it;
    /// so a second expire() of the same block sees its own rows coming back as sentinels. It skips
    /// them, reports nothing retired, and issues no deletes.
    ///
    /// @param boundaryPolicy which end of the chain this call is discarding from, and therefore
    ///        whether the retention boundary moves. DEFAULTS to Keep, the answer for every caller
    ///        that is not the commit path.
    ///
    ///        **Precondition for Advance: at most ONE Advance call per @p batch, and @p block must
    ///        be the oldest block leaving the window.** The max that keeps the boundary from moving
    ///        down is computed against @p backend, which does not yet see this batch's write, so
    ///        two Advance calls in one batch both read the pre-batch value and the second one wins
    ///        outright — a lower second block would then lower the boundary. The commit path issues
    ///        exactly one expiry per block, which is what makes the max claim true; a caller that
    ///        needs several must expire them lowest-first in separate batches, or pass Keep for all
    ///        but the highest.
    template <QueryableStateStorage ReadStorage, WritableStateStorage WriteStorage>
    task::Task<ExpireReport> expire(ReadStorage& backend, WriteStorage& batch,
        protocol::BlockNumber block,
        RetentionBoundary boundaryPolicy = RetentionBoundary::Keep) const
    {
        std::vector<bcos::bytes> keys;
        auto scan = co_await scanBlock(backend, block, DeletedShardPolicy::Skip,
            [&](ShardRecord const& record, HistoryVersion const&) {
                keys.emplace_back(record.key.begin(), record.key.end());
            });

        ExpireReport report{
            .keyCount = keys.size(), .shardsDeleted = scan.shardsRead, .retired = std::nullopt};
        if (!scan.rowKeys.empty())
        {
            report.retired = RetiredBlock{
                .block = block, .keys = std::move(keys), .shardsDeleted = scan.shardsRead};
            co_await storage2::removeSome(batch, std::move(scan.rowKeys));
        }

        // Under Advance the boundary moves with the deletes, in the same batch and therefore the
        // same Write. Doing it inside expire rather than at the call site is what makes it
        // impossible for the commit path to expire without advancing it.
        //
        // The new boundary is @p block ITSELF, not block + 1. "The value at B" is answered by the
        // first change AFTER B, so block B's own records are not what answers B — they answer the
        // blocks below it. Dropping block B therefore leaves B answerable (from B+1's records,
        // which are still here) and takes B-1 away.
        //
        // Written whenever the policy allows it, including when the block had nothing to expire:
        // the claim is "nothing below @p block is left", which is true either way.
        //
        // MAX, not assignment. The boundary only ever grows — expired data does not come back —
        // and @p block is not monotonic across every caller: a chain re-committing after a
        // rollback replays lower heights, and an operator RAISING mpt_history_*_blocks makes N - H
        // jump backwards on the next commit. Assigning would then claim heights are intact whose
        // shards an earlier expiry already deleted.
        if (boundaryPolicy == RetentionBoundary::Advance)
        {
            auto const current = co_await retentionBoundary(backend);
            if (!current || *current < block)
            {
                co_await writeRetentionBoundary(batch, block);
            }
        }
        co_return report;
    }

    /// The oldest block this store can still ANSWER FOR, read off disk — everything below it has
    /// been expired away. Nullopt when the row was never written, which means no expiry has ever
    /// run against this store.
    template <class Storage>
    static task::Task<std::optional<protocol::BlockNumber>> retentionBoundary(Storage& backend)
    {
        auto row = co_await storage2::readOne(
            backend, executor_v1::StateKey{Tables.boundary, kRetentionBoundaryRowKey});
        if (!row)
        {
            co_return std::nullopt;
        }
        co_return decodeRetentionBoundary(row->get());
    }

    /// Record that @p oldestIntactBlock is the oldest block this store can still answer for.
    ///
    /// It must ride the same batch as whatever made that true. expire() calls it itself, so an
    /// expiry cannot land without the boundary moving with it.
    template <WritableStateStorage Storage>
    static task::Task<void> writeRetentionBoundary(
        Storage& batch, protocol::BlockNumber oldestIntactBlock)
    {
        std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> rows;
        rows.emplace_back(executor_v1::StateKey{Tables.boundary, kRetentionBoundaryRowKey},
            executor_v1::StateValue{retentionBoundaryValue(oldestIntactBlock)});
        co_await storage2::writeSome(batch, std::move(rows));
    }

    /// Recompute the whole in-memory index from the shard rows on disk (layout spec §1.5).
    ///
    /// Runs once, at startup, BEFORE any reader exists — which is what removes the window the
    /// derived index would otherwise have: there is no moment at which a query can miss a version
    /// because the rebuild has not reached it yet and read that miss as "unmodified".
    ///
    /// It starts at `boundary + 1` rather than at block 0 so that rows left below the boundary by
    /// an interrupted expiry are skipped instead of resurrecting heights the store has already
    /// promised not to answer for.
    ///
    /// Every inconsistency is fatal, and deliberately so: a shard row before its meta row, a
    /// missing or extra shard, a record count that disagrees with the meta row, an unknown format
    /// version, a truncated record. Each of them means some pre-image would be absent from the
    /// index, and an absent pre-image reads as "the key never changed" (G6). The caller catches
    /// MPTInvariantViolation and calls markUnavailable() — a store that refuses is recoverable, a
    /// store that answers from a short index is not.
    ///
    /// @p Storage is constrained on seeking only, per the spec; the boundary read below also needs
    /// `readOne`, which every backend this runs on has (readAt requires it on the same storage).
    template <SeekableStateStorage Storage>
    task::Task<RebuildReport> rebuild(Storage& backend)
    {
        auto const boundary = co_await retentionBoundary(backend);
        auto const start =
            boundary ? std::max<protocol::BlockNumber>(0, *boundary + 1) : protocol::BlockNumber{0};

        HistoryIndex rebuilt;
        rebuilt.setBoundary(boundary);

        RebuildReport report;
        StagedBlock staged;
        bool inBlock = false;
        std::size_t shardsSeen = 0;
        std::size_t recordsSeen = 0;

        // Close off the block the walk has been accumulating, checking it against its meta row.
        auto finishBlock = [&]() {
            if (!inBlock)
            {
                return;
            }
            if (shardsSeen != staged.meta.shardCount || recordsSeen != staged.meta.recordCount)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation()
                    << bcos::errinfo_comment(
                           "history block holds a different number of shards or records than its "
                           "meta row declares")
                    << errinfo_historyBlock(staged.block));
            }
            rebuilt.publish(std::move(staged), std::nullopt, std::nullopt);
            ++report.blocks;
            report.records += recordsSeen;
            inBlock = false;
        };

        // Every throw below carries the block it happened at (errinfo_historyBlock), because the
        // caller that reports a failed rebuild is the B.10 audit and "the index cannot be rebuilt"
        // is not actionable without the height to look at. The row key is decoded FIRST, before the
        // sentinel check, so even that case can name its block.
        report.bytesScanned = co_await walkShardTable(backend, Tables.shard, metaRowKey(start),
            [&](executor_v1::StateKeyView const& rowKeyView,
                executor_v1::StateValue const* entry) -> bool {
                auto const rowBlock = rowKeyBlock(rowKeyView.m_key);
                if (entry == nullptr)
                {
                    BOOST_THROW_EXCEPTION(
                        MPTInvariantViolation()
                        << bcos::errinfo_comment("history shard table holds a deletion sentinel; "
                                                 "the records it carried cannot be indexed")
                        << errinfo_historyBlock(rowBlock));
                }
                if (isMetaRowKey(rowKeyView.m_key, rowBlock))
                {
                    finishBlock();
                    staged = StagedBlock{
                        .block = rowBlock, .meta = decodeMeta(entry->get()), .versions = {}};
                    staged.versions.reserve(staged.meta.recordCount);
                    shardsSeen = 0;
                    recordsSeen = 0;
                    inBlock = true;
                    return true;
                }
                if (!inBlock || rowBlock != staged.block)
                {
                    BOOST_THROW_EXCEPTION(
                        MPTInvariantViolation()
                        << bcos::errinfo_comment(
                               "history shard row is not preceded by its block's meta row")
                        << errinfo_historyBlock(rowBlock));
                }
                auto const shard = rowKeyShard(rowKeyView.m_key);
                if (shard != shardsSeen)
                {
                    BOOST_THROW_EXCEPTION(
                        MPTInvariantViolation()
                        << bcos::errinfo_comment(
                               "history shard ordinals are not the contiguous 0..n-1 run the meta "
                               "row describes")
                        << errinfo_historyBlock(rowBlock));
                }
                ++shardsSeen;
                if (shardsSeen > staged.meta.shardCount)
                {
                    BOOST_THROW_EXCEPTION(
                        MPTInvariantViolation()
                        << bcos::errinfo_comment(
                               "history block holds more shards than its meta row declares")
                        << errinfo_historyBlock(rowBlock));
                }
                for (auto const& record : decodeShard(entry->get()))
                {
                    staged.versions.emplace_back(bcos::bytes(record.key.begin(), record.key.end()),
                        HistoryVersion{.block = rowBlock,
                            .shard = static_cast<uint16_t>(shard),
                            .offset = static_cast<uint32_t>(record.offset)});
                    ++recordsSeen;
                }
                return true;
            });
        finishBlock();

        m_index.replace(std::move(rebuilt));
        co_return report;
    }

private:
    /// What a block walk does with a row that is present but carries a deletion sentinel instead
    /// of bytes — the shape an expired row has on a logical-deletion layer. The row is the same;
    /// the two callers need opposite readings of it, so the choice is a parameter rather than a
    /// rule baked into the walk.
    enum class DeletedShardPolicy : uint8_t
    {
        /// The caller needs the block's complete diff (readBlock, and through it rollback and the
        /// B.10 audits). A deleted row makes part of that diff unreadable, so fail loud (G6).
        Reject,
        /// The caller only re-issues deletes (expire). A row that is already a sentinel has
        /// nothing left to clean up, and skipping it is what makes a replay a no-op.
        Skip,
    };

    /// What one single-block walk saw.
    struct BlockScan
    {
        bool metaFound{};
        BlockMeta meta;
        std::size_t shardsRead{};
        std::size_t recordsRead{};
        /// Every LIVE row of the block — the meta row and the shard rows — as the keys an expiry
        /// deletes.
        std::vector<executor_v1::StateKey> rowKeys;
    };

    /// spec B.3, first two lines. Kept as its own function so the one call site above can be
    /// compiled out wholesale for the negative-control test.
    static void checkWindow(
        protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
    {
        if (depth < 0)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "history retention depth must not be negative"));
        }
        if (block < 0)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "history block number must not be negative"));
        }
        // Signed arithmetic on purpose: a chain shorter than the window makes tip - depth + 1
        // negative, which correctly admits every block down to genesis.
        if (block < tip - depth + 1)
        {
            BOOST_THROW_EXCEPTION(HistoryPruned() << bcos::errinfo_comment(
                                      "requested block is older than the retained history "
                                      "window"));
        }
        if (block > tip)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "requested block is ahead of the chain tip"));
        }
    }

    /// Walk exactly one block's rows, handing each record to @p sink as (record, version).
    /// @p sink lets each caller materialize only what it needs: readBlock copies the values,
    /// expire copies only the keys, and neither pays for the other's copies.
    template <SeekableStateStorage Storage, class RecordSink>
    static task::Task<BlockScan> scanBlock(Storage& backend, protocol::BlockNumber block,
        DeletedShardPolicy deletedShards, RecordSink&& sink)
    {
        BlockScan scan;
        co_await walkShardTable(backend, Tables.shard, metaRowKey(block),
            [&](executor_v1::StateKeyView const& rowKeyView,
                executor_v1::StateValue const* entry) -> bool {
                if (rowKeyBlock(rowKeyView.m_key) != block)
                {
                    return false;
                }
                if (entry == nullptr)
                {
                    if (deletedShards == DeletedShardPolicy::Skip)
                    {
                        // Live rows can follow a deleted one, so the walk goes on.
                        return true;
                    }
                    BOOST_THROW_EXCEPTION(
                        MPTInvariantViolation() << bcos::errinfo_comment(
                            "history block row is deleted; the records it carried cannot be "
                            "recovered from this storage layer"));
                }
                scan.rowKeys.emplace_back(rowKeyView);
                if (isMetaRowKey(rowKeyView.m_key, block))
                {
                    scan.meta = decodeMeta(entry->get());
                    scan.metaFound = true;
                    return true;
                }
                auto const shard = rowKeyShard(rowKeyView.m_key);
                ++scan.shardsRead;
                for (auto const& record : decodeShard(entry->get()))
                {
                    sink(record, HistoryVersion{.block = block,
                                     .shard = static_cast<uint16_t>(shard),
                                     .offset = static_cast<uint32_t>(record.offset)});
                    ++scan.recordsRead;
                }
                return true;
            });
        co_return scan;
    }

    HistoryIndex m_index;
};

/// The two instantiations spec B.8 calls for.
using StateHistoryStore = ReverseHistoryStore<kStateHistory>;
using TrieHistoryStore = ReverseHistoryStore<kTrieHistory>;

}  // namespace bcos::ledger::mpt::history
