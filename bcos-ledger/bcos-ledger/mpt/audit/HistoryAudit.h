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
 * @file HistoryAudit.h
 * @brief The five things a reverse history must be audited for (pathdb spec B.10, restated for
 *        the optimized layout)
 *
 * The earlier layout kept a `(key, block)` index ROW next to every manifest entry, so B.10 ①
 * compared two on-disk sets against each other. This layout has no index table at all — the
 * `(key, block)` order lives in RAM and is rebuilt from the shards at startup (HistoryIndex.h) —
 * so the same five questions are asked of what the optimized layout actually holds:
 *
 *   ① each block's Meta row agrees with the shard rows that follow it: the declared shard count,
 *      the contiguous 0..n-1 ordinals, and the declared record count;
 *   ② every block inside the retention window has a Meta row;
 *   ③ a rebuild over the retained range SUCCEEDS and produces exactly Σ recordCount versions —
 *      this is the check that the in-memory index a restarting node will build can answer for
 *      every pre-image on disk, and it replaces the old "every listed key has an index row";
 *   ④ the retention-boundary row equals `max(0, oldestRetained - 1)`, and the retained span is
 *      the window the configured depth describes;
 *   ⑤ each Meta row's block hash equals the hash the LEDGER records at that height — the check
 *      that these retained pre-images belong to this chain rather than to a fork the node was on
 *      before a rollback.
 */
#pragma once

#include "../Errors.h"
#include "../history/HistoryErrors.h"
#include "../history/HistoryRowCodec.h"
#include "../history/HistoryTables.h"
#include "../history/ReverseHistoryStore.h"
#include "../history/ShardTableWalk.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/get_error_info.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ledger::mpt::audit
{

/// Which of spec B.10's checks a finding comes from.
enum class HistoryFindingKind : std::uint8_t
{
    /// B.10 ①: the block holds a different number of shard rows than its Meta row declares. Some
    /// of the block's pre-images are unreadable, and a rebuild refuses the whole store because of
    /// it — the index cannot be built at all while this is true.
    MetaShardCountMismatch,
    /// B.10 ①: the shard rows decode to a different number of records than the Meta row declares.
    /// Same consequence as above, from the other field.
    MetaRecordCountMismatch,
    /// B.10 ①: the shard ordinals are not the contiguous `0..n-1` run the layout promises. The
    /// walk that rebuilds the index stops at the first gap, so everything after it is lost even
    /// though the rows are still there.
    ShardOrdinalGap,
    /// B.10 ②: a block inside the retention window has no Meta row. The fatal one: a query for a
    /// height below the gap is answered by the first change ABOVE it, and the changes the missing
    /// block recorded are exactly what is no longer there to be found — so the answer is a value
    /// from the wrong height rather than a refusal.
    MissingMeta,
    /// B.10 ③: rebuilding the in-memory index over the retained range threw. A node restarting on
    /// this store marks the history Unavailable and refuses every historical read (G10). The
    /// detail carries the rebuild's own message, which names what it tripped over.
    RebuildRejected,
    /// B.10 ④: the span of blocks that actually have Meta rows disagrees with the window the
    /// caller's retention depth H describes.
    RetentionBoundaryMismatch,
    /// B.10 ④, the other half: the store's own retention-boundary ROW disagrees with the Meta
    /// rows it holds — or is missing while Meta rows exist. The boundary is what every serving
    /// query consults (HistoryIndex::locate, seeded from this row by rebuild), so a boundary that
    /// claims MORE than the shards support is how a historical read comes back with today's value
    /// under an old block number.
    RetentionBoundaryRowMismatch,
    /// B.10 ⑤: a Meta row's block hash is not the hash the ledger records at that height. The
    /// pre-images retained for that height were captured on a different block — a fork the node
    /// followed before a rollback — so reverse-applying them would move the state plane onto a
    /// chain this node is not on.
    BlockHashMismatch,
    /// B.10 ⑤, unproven rather than wrong: the ledger has no readable hash for a height that has
    /// a Meta row, so the identity of those pre-images could not be checked. Reported rather than
    /// passed over in silence — "could not compare" is not "the hashes agree" (G6).
    BlockHashUnverifiable,
};

/// The B.10 item, as text, for a report an operator reads.
inline std::string_view describe(HistoryFindingKind kind) noexcept
{
    switch (kind)
    {
    case HistoryFindingKind::MetaShardCountMismatch:
        return "B.10 (1) block holds a different number of shards than its meta row declares";
    case HistoryFindingKind::MetaRecordCountMismatch:
        return "B.10 (1) block holds a different number of records than its meta row declares";
    case HistoryFindingKind::ShardOrdinalGap:
        return "B.10 (1) shard ordinals are not the contiguous 0..n-1 run";
    case HistoryFindingKind::MissingMeta:
        return "B.10 (2) a block in the window has no meta row";
    case HistoryFindingKind::RebuildRejected:
        return "B.10 (3) the in-memory index cannot be rebuilt from these shards";
    case HistoryFindingKind::RetentionBoundaryMismatch:
        return "B.10 (4) retained span disagrees with the configured depth";
    case HistoryFindingKind::RetentionBoundaryRowMismatch:
        return "B.10 (4) the retention-boundary row disagrees with the meta rows";
    case HistoryFindingKind::BlockHashMismatch:
        return "B.10 (5) meta row block hash is not the ledger's hash at that height";
    case HistoryFindingKind::BlockHashUnverifiable:
        return "B.10 (5) no ledger block hash to check this block's meta row against";
    }
    return "unknown history finding";
}

/// One inconsistency, with enough detail to locate it.
///
/// Block-level throughout, and that is a consequence of the layout rather than a simplification:
/// the optimized store holds no per-key row to be inconsistent on its own. A key's pre-image is a
/// byte range inside its block's shard, so every way it can be damaged is a statement about the
/// block — the meta row, a shard, or the walk that reads them.
struct HistoryFinding
{
    HistoryFindingKind kind{};
    protocol::BlockNumber block{};
    /// What is wrong, when the kind and the block do not say it on their own. Empty otherwise.
    /// Explicitly default-initialized so that a designated-initializer that omits it does not trip
    /// GCC's -Wmissing-field-initializers.
    std::string detail{};
};

/// How the audit learns the hash the CHAIN records for a height — B.10 ⑤'s other side.
///
/// A callable rather than a table read, because the answer lives outside this component: the
/// caller owns the ledger rows and knows which of them is canonical (`s_number_2_hash`, written by
/// both commit paths with exactly the hash they hand `stageBlockHistory`). Returning nullopt means
/// "this height has no readable hash", which the audit reports as unverifiable rather than
/// treating as a match.
///
/// An EMPTY source means the caller cannot supply hashes at all, and ⑤ is then not run: the report
/// says `blockHashChecked == false` and carries no ⑤ findings, the same way the path-tree audit
/// reports `rootChecked == false` when no committed root was available.
using BlockHashSource = std::function<std::optional<bcos::h256>(protocol::BlockNumber)>;

/// What one auditHistory() pass found.
///
/// Findings are RETURNED rather than thrown, unlike the path-tree audit. The two are different
/// questions: a hole in the tree stops the walk that found it, while the history window is a set
/// of independent blocks and an operator deciding how far to replay needs the whole list, not the
/// first entry of it. Call throwIfInconsistent() at a site that must fail loud (G6).
struct HistoryAuditReport
{
    protocol::BlockNumber tip{};
    protocol::BlockNumber depth{};
    /// The window B.10 ② and ④ are judged against: [windowStart, tip].
    protocol::BlockNumber windowStart{};
    /// Span of blocks that actually carry a Meta row. Both are -1 when there are none.
    protocol::BlockNumber oldestRetained{-1};
    protocol::BlockNumber newestRetained{-1};
    /// The store's retention-boundary row: the oldest block it claims it can still ANSWER FOR.
    /// Nullopt when the row was never written, which for a store holding Meta rows is itself a
    /// finding.
    std::optional<protocol::BlockNumber> retentionBoundary{};
    /// Blocks with a Meta row.
    std::size_t blocksWithMeta{};
    /// Shard rows across every block.
    std::size_t shardRows{};
    /// Records the shard rows actually decode to.
    std::size_t records{};
    /// Records the Meta rows declare. Equal to `records` on a healthy store; the two are reported
    /// separately so an operator can see WHICH side of a ① finding is short.
    std::size_t declaredRecords{};

    /// ---- B.10 ③, the rebuild ----
    /// Whether the rebuild completed. False means it threw and a RebuildRejected finding says why.
    bool rebuilt{};
    std::size_t rebuiltBlocks{};
    std::size_t rebuiltRecords{};
    std::size_t bytesScanned{};
    /// Versions the rebuilt index holds — what a restarting node would be able to answer from.
    std::size_t indexVersions{};
    /// Records in the blocks the rebuild is ACCOUNTABLE for: the ones above the retention
    /// boundary, which is where its walk starts. `indexVersions` is checked against this.
    ///
    /// Not the same as `declaredRecords`, and the gap between the two is the point: records below
    /// the boundary are on disk and outside every index a restart can build. On a healthy store
    /// the gap is zero at every height except the boundary's own — a store seeded at its first
    /// block, or expired down to one, keeps that block's records for the height BELOW it, which
    /// no longer exists to be queried.
    std::size_t accountableRecords{};
    /// Wall time the rebuild took. The number an operator sizes a restart with, so it is measured
    /// here rather than guessed at from the row counts.
    std::size_t rebuildMilliseconds{};

    /// ---- B.10 ⑤ ----
    /// Whether the caller supplied a hash source at all. False means the retained blocks' chain
    /// identity was NOT checked — the audit proved the rows agree with each other and with the
    /// configured window, nothing about which chain they came from.
    bool blockHashChecked{};

    std::vector<HistoryFinding> findings{};

    [[nodiscard]] bool consistent() const noexcept { return findings.empty(); }

    /// spec §13: a hole anywhere in the history chain must stop history service loudly rather
    /// than fall back to the current value.
    void throwIfInconsistent() const
    {
        if (findings.empty())
        {
            return;
        }
        auto const& first = findings.front();
        std::string message = "history audit found " + std::to_string(findings.size()) +
                              " inconsistencies; first: " + std::string(describe(first.kind)) +
                              " at block " + std::to_string(first.block);
        if (!first.detail.empty())
        {
            message += " — " + first.detail;
        }
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(message));
    }
};

namespace detail
{

/// What one block's rows look like on disk, as the audit's tolerant walk saw them.
///
/// Tolerant is the whole difference from ReverseHistoryStore::rebuild, which walks the same rows
/// and THROWS at the first disagreement. The audit has to keep going: an operator needs the list
/// of damaged blocks, not the first one.
struct BlockRows
{
    bool metaFound{};
    history::BlockMeta meta;
    /// Shard ordinals, in the order the walk met them — which is ascending, since the seek walks
    /// row keys in byte order and the ordinal is the key's low field.
    std::vector<std::size_t> shardOrdinals;
    /// Records decoded out of those shards.
    std::size_t records{};
};

/// Every block that has any row in @p Tables' shard table.
using ShardTableScan = std::map<protocol::BlockNumber, BlockRows>;

/// One seek-scan of @p Tables' shard table, grouping rows by block.
///
/// The walk itself is `history::walkShardTable` — the SAME function ReverseHistoryStore's rebuild
/// and whole-block read go through. That is not code tidiness: this is the component whose job is
/// to detect layout damage, and reading the layout through a second implementation of the walk
/// would let it agree with itself while disagreeing with the store it is auditing.
///
/// What is this scan's own is the visitor. It starts at row key "" rather than at the retention
/// boundary, because rows an interrupted expiry left BELOW the boundary are invisible to rebuild
/// (which starts at boundary + 1) and are exactly what B.10 ④'s "dead weight" direction is about.
/// And it SKIPS a deletion sentinel rather than refusing it, the opposite of what rebuild does with
/// the same row: an expired row on a logical-deletion layer is already gone, so the audit reports
/// the block as short a shard — while rebuild's refusal over the same row is reported separately,
/// as B.10 ③.
///
/// @throws MPTInvariantViolation on a row the codec cannot decode at all — a row key that is
///         neither 8 nor 10 bytes, a Meta row that is not the fixed 41 bytes or carries an unknown
///         format version, a shard payload that ends inside a record. Those are not
///         "inconsistencies between two sets"; they mean the scan cannot produce the set at all.
template <history::HistoryTables const& Tables, history::SeekableStateStorage Storage>
bcos::task::Task<ShardTableScan> scanShardTable(Storage& storage)
{
    ShardTableScan blocks;
    co_await history::walkShardTable(storage, Tables.shard, "",
        [&](executor_v1::StateKeyView const& rowKeyView,
            executor_v1::StateValue const* entry) -> bool {
            if (entry == nullptr)
            {
                return true;  // an expired row on a logical-deletion layer: it is already gone
            }
            auto const block = history::rowKeyBlock(rowKeyView.m_key);
            auto& blockRows = blocks[block];
            if (history::isMetaRowKey(rowKeyView.m_key, block))
            {
                blockRows.meta = history::decodeMeta(entry->get());
                blockRows.metaFound = true;
                return true;
            }
            blockRows.shardOrdinals.push_back(history::rowKeyShard(rowKeyView.m_key));
            blockRows.records += history::decodeShard(entry->get()).size();
            return true;
        });
    co_return blocks;
}

}  // namespace detail

/// Audit one reverse-history instance (spec B.10 ①-⑤).
///
/// Two passes over the shard table: a tolerant one that groups the rows by block and answers ①,
/// ②, ④ and ⑤, and then the store's own `rebuild`, whose success IS ③. The second pass is not a
/// duplicate of the first — rebuild refuses at the first disagreement and the audit must not, so
/// what they prove differs: the first says WHICH blocks are damaged, the second says whether a
/// restarting node could serve this store at all.
///
/// **Offline.** It constructs a throwaway store to run the rebuild in, so the index it builds is
/// discarded with it and no live index is touched. Running it against a node's RocksDB while that
/// node is up would read a moving store.
///
/// @param tip the chain's current block number.
/// @param depth this instance's retention depth — H_state or H_proof. Passed in, never read from
///        the store: it is a node-local operational parameter, and B.10 ④ is exactly the check
///        that what is on disk agrees with what the operator believes it configured.
/// @param firstHistoryBlock the earliest block this node ever wrote history for. Blocks before it
///        are not expected to have Meta rows, so the window starts no earlier. Defaults to
///        genesis; a node that turned history on mid-life must pass the block it turned it on at,
///        or every pre-history block is reported as a B.10 ② hole. Same for a node that ran with
///        depth 0 for a while — the coverage contract in HistoryTables.h spells out when a block
///        legitimately has no Meta row.
/// @param blockHashAt the ledger's hash at a height, for ⑤. Empty (the default) skips ⑤ entirely
///        and sets `blockHashChecked = false`.
/// @throws MPTInvariantViolation when a row is structurally undecodable (see scanShardTable).
template <history::HistoryTables const& Tables, history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0,
    BlockHashSource blockHashAt = {})
{
    HistoryAuditReport report{.tip = tip, .depth = depth};
    report.windowStart = std::max<protocol::BlockNumber>(firstHistoryBlock, tip - depth + 1);
    report.blockHashChecked = static_cast<bool>(blockHashAt);

    // ---- pass 1: the rows themselves, grouped by block ----
    auto const blocks = co_await detail::scanShardTable<Tables>(storage);
    for (auto const& [block, rows] : blocks)
    {
        report.shardRows += rows.shardOrdinals.size();
        report.records += rows.records;
        if (!rows.metaFound)
        {
            continue;
        }
        ++report.blocksWithMeta;
        report.declaredRecords += rows.meta.recordCount;
        if (report.oldestRetained < 0)
        {
            report.oldestRetained = block;
        }
        report.newestRetained = block;
    }

    // ---- B.10 ①: each Meta row against the shards that follow it ----
    for (auto const& [block, rows] : blocks)
    {
        if (!rows.metaFound)
        {
            // Shard rows with no Meta row in front of them. Reported as the ② hole it is: the
            // block is not readable as a block, whatever bytes are sitting under it. The rebuild
            // refuses the whole store over this, and the ③ finding below says so separately.
            report.findings.push_back(HistoryFinding{.kind = HistoryFindingKind::MissingMeta,
                .block = block,
                .detail = std::to_string(rows.shardOrdinals.size()) +
                          " shard rows exist for this block but it has no meta row, so nothing "
                          "says how many of them to expect"});
            continue;
        }
        // Ascending by construction (the walk is in row-key order), so contiguity from 0 is the
        // single comparison `ordinals[i] == i`.
        for (std::size_t index = 0; index < rows.shardOrdinals.size(); ++index)
        {
            if (rows.shardOrdinals[index] != index)
            {
                report.findings.push_back(HistoryFinding{
                    .kind = HistoryFindingKind::ShardOrdinalGap,
                    .block = block,
                    .detail = "shard " + std::to_string(index) +
                              " is missing; the next row found "
                              "is shard " +
                              std::to_string(rows.shardOrdinals[index]) +
                              ". A rebuild stops at the gap, so every record above it is lost"});
                break;
            }
        }
        if (rows.shardOrdinals.size() != rows.meta.shardCount)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::MetaShardCountMismatch,
                    .block = block,
                    .detail = "meta row declares " + std::to_string(rows.meta.shardCount) +
                              " shards, " + std::to_string(rows.shardOrdinals.size()) +
                              " are on disk"});
        }
        if (rows.records != rows.meta.recordCount)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::MetaRecordCountMismatch,
                    .block = block,
                    .detail = "meta row declares " + std::to_string(rows.meta.recordCount) +
                              " records, the shards decode to " + std::to_string(rows.records)});
        }
    }

    // ---- B.10 ②: the window must be gapless ----
    //
    // Contiguous gaps are folded into ONE finding. The common shapes here are whole eras, not
    // isolated blocks — a node that turned history on mid-life and was audited without --from, a
    // depth raised across a restart, a chain older than the feature — and one finding per block
    // would bury the other four checks under thousands of lines saying the same thing. The range
    // is what an operator acts on anyway.
    //
    // A block that IS in the scan without a Meta row is not folded in: it produced its own finding
    // above, with the shard count that makes it a different fault (rows exist, nothing declares
    // them) from a block that is simply absent.
    for (auto block = report.windowStart; block <= tip; ++block)
    {
        if (blocks.contains(block))
        {
            continue;
        }
        auto const gapStart = block;
        while (block + 1 <= tip && !blocks.contains(block + 1))
        {
            ++block;
        }
        report.findings.push_back(HistoryFinding{.kind = HistoryFindingKind::MissingMeta,
            .block = gapStart,
            .detail = gapStart == block ? std::string{} :
                                          "blocks " + std::to_string(gapStart) + ".." +
                                              std::to_string(block) + " have no meta row (" +
                                              std::to_string(block - gapStart + 1) + " blocks)"});
    }

    // ---- B.10 ⑤: the retained pre-images must belong to THIS chain ----
    //
    // A Meta row's hash is the hash the commit path handed stageBlockHistory, which is the same
    // value that commit writes into the ledger's number->hash row. They disagree only when the
    // history retained here was captured on a block this chain no longer has — the state after a
    // rollback that moved the chain but left the old pre-images behind. Reverse-applying those
    // would walk the state plane onto the abandoned fork, which is why this is checked before an
    // operator is allowed to trust a rollback plan.
    if (blockHashAt)
    {
        for (auto const& [block, rows] : blocks)
        {
            if (!rows.metaFound)
            {
                continue;
            }
            auto const ledgerHash = blockHashAt(block);
            if (!ledgerHash)
            {
                report.findings.push_back(
                    HistoryFinding{.kind = HistoryFindingKind::BlockHashUnverifiable,
                        .block = block,
                        .detail = "the meta row records block hash " + rows.meta.blockHash.hex() +
                                  " and the ledger has no readable hash at this height to compare "
                                  "it with"});
                continue;
            }
            if (*ledgerHash != rows.meta.blockHash)
            {
                report.findings.push_back(
                    HistoryFinding{.kind = HistoryFindingKind::BlockHashMismatch,
                        .block = block,
                        .detail = "the meta row records block hash " + rows.meta.blockHash.hex() +
                                  " but the ledger records " + ledgerHash->hex() +
                                  " at this height; these pre-images were captured on a different "
                                  "block"});
            }
        }
    }

    // ---- B.10 ④: the retained span must be the window the depth describes ----
    // Only when something is retained: an entirely empty history is already fully reported by the
    // B.10 ② findings above, and adding a boundary finding on top would just be noise.
    if (report.blocksWithMeta != 0)
    {
        if (report.oldestRetained != report.windowStart)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryMismatch,
                    .block = report.oldestRetained});
        }
        if (report.newestRetained != tip)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryMismatch,
                    .block = report.newestRetained});
        }
    }

    // ---- B.10 ④, the store's own metadata: the boundary ROW against the Meta rows ----
    //
    // The boundary records the oldest block the store can still ANSWER FOR, and one invariant
    // covers every regime the store reaches:
    //
    //     boundary == max(0, oldestRetained - 1)
    //
    //  - after an expiry of block E: the boundary is E and E's rows are gone, so the oldest
    //    surviving Meta row is E + 1 (ReverseHistoryStore::expire, RetentionBoundary::Advance);
    //  - on a chain younger than the window, nothing has expired and the boundary is the seed
    //    max(0, firstRecorded - 1), with firstRecorded the oldest Meta row
    //    (HistoryCommit.h::seedRetentionBoundary);
    //  - a Keep-rollback trims blocks off the TOP and leaves the boundary alone, so the oldest
    //    is untouched either way.
    //
    // The clamp matters at genesis: a store seeded at block 0 writes boundary 0 while its oldest
    // Meta row is also 0, and an unclamped `oldest - 1` would read that healthy store as broken.
    //
    // Judged only when Meta rows exist. A store with none has no oldest to compare against, and
    // whether that is a fault at all is already answered by the B.10 ② findings above.
    report.retentionBoundary =
        co_await history::ReverseHistoryStore<Tables>::retentionBoundary(storage);
    if (report.blocksWithMeta != 0)
    {
        auto const expectedBoundary = std::max<protocol::BlockNumber>(0, report.oldestRetained - 1);
        if (!report.retentionBoundary)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryRowMismatch,
                    .block = expectedBoundary,
                    .detail = "this store holds meta rows but has no retention-boundary row; the "
                              "rebuild seeds the index's boundary from that row and every serving "
                              "query consults it, so an absent one means the store cannot say "
                              "which heights it still answers for"});
        }
        else if (*report.retentionBoundary != expectedBoundary)
        {
            // The two directions are not equally bad, and the report says which one this is.
            auto const detailText =
                *report.retentionBoundary < expectedBoundary ?
                    std::string("the boundary claims heights this store no longer holds (row " +
                                std::to_string(*report.retentionBoundary) + ", oldest meta row " +
                                std::to_string(report.oldestRetained) +
                                "); a historical read below the oldest meta row finds no version "
                                "and answers with TODAY's value under an old block number") :
                    std::string("the boundary is above the oldest meta row (row " +
                                std::to_string(*report.retentionBoundary) + ", oldest meta row " +
                                std::to_string(report.oldestRetained) +
                                "); those blocks are unreachable — refused by the boundary check "
                                "inside the index — so they are dead weight rather than a wrong "
                                "answer");
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryRowMismatch,
                    .block = *report.retentionBoundary,
                    .detail = detailText});
        }
    }

    // ---- B.10 ③: a restarting node must be able to rebuild an index that answers for all of it
    //
    // This is what replaced "every key a manifest lists has an index row". The index is DERIVED
    // now, so the question is no longer whether a second set of rows agrees with the first — it is
    // whether the one set on disk still yields the index a restart will serve from. Damaged rows
    // make the walk REFUSE, which is what this reports: a node restarting on such a store comes up
    // with its history Unavailable, and that is the outcome an operator is being warned about.
    {
        history::ReverseHistoryStore<Tables> rebuildProbe;
        auto const started = std::chrono::steady_clock::now();
        try
        {
            auto const rebuild = co_await rebuildProbe.rebuild(storage);
            report.rebuilt = true;
            report.rebuiltBlocks = rebuild.blocks;
            report.rebuiltRecords = rebuild.records;
            report.bytesScanned = rebuild.bytesScanned;
            report.indexVersions = rebuildProbe.index().versionCount();
        }
        catch (MPTInvariantViolation const& error)
        {
            // The block comes off the exception, not from a guess: rebuild tags every refusal it
            // raises itself with the height it tripped at (errinfo_historyBlock), and a finding
            // pointing at the wrong height sends an operator to the wrong rows. Absent only for a
            // refusal raised inside the row codec, which never saw a row key — and the pass-1 scan
            // above decodes every row first, so a codec failure has already thrown out of this
            // whole function before the rebuild runs.
            auto const* refusedAt = boost::get_error_info<history::errinfo_historyBlock>(error);
            report.findings.push_back(HistoryFinding{.kind = HistoryFindingKind::RebuildRejected,
                .block = refusedAt != nullptr ? *refusedAt : report.oldestRetained,
                .detail = std::string(boost::diagnostic_information(error))});
        }
        report.rebuildMilliseconds =
            static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                    .count());

        if (report.rebuilt)
        {
            // `versionCount == Σ recordCount`, the second half of B.10 ③ — but over the blocks the
            // rebuild is ACCOUNTABLE for, which is not every block on disk: its walk starts at
            // `boundary + 1`, and records at or below the boundary answer for heights the store
            // has already promised not to serve. Counting those would report a healthy store as
            // broken at its very first block, where the seed writes `boundary = firstRecorded - 1`
            // and clamps to 0, leaving block 0's own records legitimately outside the index.
            //
            // It is an ASSERTION, not a finding, and the difference is deliberate. Every finding
            // this audit reports names a state some sequence of writes, crashes or corruptions can
            // put the rows in. This one cannot: rebuild verifies each block's record count against
            // that block's own meta row and throws otherwise, so reaching this line already means
            // every block it visited matched, and the index gets exactly one version per record
            // published. A disagreement here would mean the audit's reading of the meta rows and
            // the rebuild's reading of the same rows had diverged — a defect in one of the two
            // walks rather than in the store — so it fails loud instead of being filed against the
            // operator's data (G6).
            for (auto const& [block, rows] : blocks)
            {
                if (rows.metaFound &&
                    (!report.retentionBoundary || block > *report.retentionBoundary))
                {
                    report.accountableRecords += rows.meta.recordCount;
                }
            }
            if (report.indexVersions != report.accountableRecords)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "history audit: the rebuilt index holds " +
                        std::to_string(report.indexVersions) +
                        " versions while this audit's own read of the same meta rows accounts "
                        "for " +
                        std::to_string(report.accountableRecords) +
                        " above the retention boundary; the two walks of the shard table disagree "
                        "and neither number can be reported"));
            }
        }
    }

    co_return report;
}

/// B.10 over the state history, retained for H_state blocks.
template <history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditStateHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0,
    BlockHashSource blockHashAt = {})
{
    return auditHistory<history::kStateHistory>(
        storage, tip, depth, firstHistoryBlock, std::move(blockHashAt));
}

/// B.10 over the trie-node history, retained for H_proof blocks.
template <history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditTrieHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0,
    BlockHashSource blockHashAt = {})
{
    return auditHistory<history::kTrieHistory>(
        storage, tip, depth, firstHistoryBlock, std::move(blockHashAt));
}

}  // namespace bcos::ledger::mpt::audit
