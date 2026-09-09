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
 * @brief The four things a reverse-history index must be audited for (pathdb spec B.10)
 */
#pragma once

#include "../Errors.h"
#include "../history/HistoryRowCodec.h"
#include "../history/HistoryTables.h"
#include "../history/ReverseHistoryStore.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::audit
{

/// Which of spec B.10's four checks a finding comes from.
///
/// B.10 ① is bidirectional, so it produces two kinds: the two directions fail for different
/// reasons (a lost index row vs a lost manifest record) and an operator repairs them differently,
/// so collapsing them would throw away the only distinguishing information the audit has.
enum class HistoryFindingKind : std::uint8_t
{
    /// B.10 ①, manifest -> index: a block's manifest lists a key, but the (key, block) index row
    /// it promises is not there. The pre-image is gone; queries for that key across that block
    /// answer from a NEWER change or fall through to the current value.
    ManifestKeyWithoutIndexRow,
    /// B.10 ①, index -> manifest: an index row exists for (key, block) whose block DOES have a
    /// manifest, but that manifest does not list the key. Expiry of that block would leave the
    /// row behind forever — nothing else records that it exists.
    IndexRowNotInManifest,
    /// B.10 ②: a block inside the retention window has no manifest row at all. The fatal one:
    /// reverse history is a chain walked backwards from the tip, so a missing block invalidates
    /// every block older than it, not just itself.
    MissingManifest,
    /// B.10 ③: an index row whose block has no manifest — it points at a record that does not
    /// exist. Either the block was half-expired (manifest deleted before its index rows, which
    /// B.5's ordering exists to prevent) or the manifest was lost.
    OrphanIndexRow,
    /// B.10 ④: the span of blocks that actually have manifests disagrees with the window the
    /// caller's retention depth H describes.
    RetentionBoundaryMismatch,
    /// B.10 ④, the other half: the store's own retention-boundary ROW disagrees with the
    /// manifests it holds — or is missing while manifests exist. The boundary is what every
    /// serving query consults (ReverseHistoryStore::readAt's post-seek re-read,
    /// HistoryRead.h::historyCoversBlock), so a boundary that claims MORE than the manifests
    /// support is how a historical read comes back with today's value under an old block number.
    RetentionBoundaryRowMismatch,
};

/// The B.10 item, as text, for a report an operator reads.
inline std::string_view describe(HistoryFindingKind kind) noexcept
{
    switch (kind)
    {
    case HistoryFindingKind::ManifestKeyWithoutIndexRow:
        return "B.10 (1) manifest lists a key with no index row";
    case HistoryFindingKind::IndexRowNotInManifest:
        return "B.10 (1) index row is not listed by its block's manifest";
    case HistoryFindingKind::MissingManifest:
        return "B.10 (2) a block in the window has no manifest";
    case HistoryFindingKind::OrphanIndexRow:
        return "B.10 (3) index row points at a block with no manifest";
    case HistoryFindingKind::RetentionBoundaryMismatch:
        return "B.10 (4) retained span disagrees with the configured depth";
    case HistoryFindingKind::RetentionBoundaryRowMismatch:
        return "B.10 (4) the retention-boundary row disagrees with the manifests";
    }
    return "unknown history finding";
}

/// One inconsistency, with enough detail to locate it.
struct HistoryFinding
{
    HistoryFindingKind kind{};
    protocol::BlockNumber block{};
    /// Hex of the logical key, empty for findings that are about a block rather than a key.
    std::string key;
    /// What is wrong, when the kind and the block do not say it on their own. Empty otherwise.
    std::string detail;
};

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
    /// Span of blocks that actually carry a manifest. Both are -1 when there are none.
    protocol::BlockNumber oldestRetained{-1};
    protocol::BlockNumber newestRetained{-1};
    /// The store's retention-boundary row: the oldest block it claims it can still ANSWER FOR.
    /// Nullopt when the row was never written, which for a store holding manifests is itself a
    /// finding.
    std::optional<protocol::BlockNumber> retentionBoundary;
    std::size_t blocksWithManifest{};
    /// Keys listed across every manifest.
    std::size_t manifestKeys{};
    /// Rows in the index table.
    std::size_t indexRows{};
    std::vector<HistoryFinding> findings;

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
        if (!first.key.empty())
        {
            message += " key 0x" + first.key;
        }
        if (!first.detail.empty())
        {
            message += " — " + first.detail;
        }
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(message));
    }
};

namespace detail
{

/// Hex of a logical history key, for a finding an operator has to look up.
inline std::string hexKey(std::string_view key)
{
    return bcos::toHex(bcos::bytesConstRef(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<bcos::byte const*>(key.data()), key.size()));
}

/// Every block that has a manifest, mapped to the keys that manifest lists.
///
/// The bool is "has an index row for this (key, block) been seen yet?" — auditHistory ticks it off
/// during its index pass. A caller that only needs "does this block have a manifest at all" reads
/// the outer map: crucially, a block that changed nothing is PRESENT with an empty key set, while
/// a block whose manifest is lost is ABSENT. keysOfBlock() cannot tell those two apart (it returns
/// an empty vector for both), which is why rollback asks here instead.
using ManifestIndex = std::map<protocol::BlockNumber, std::unordered_map<std::string, bool>>;

/// One seek-scan of @p Tables' manifest table.
/// @throws MPTInvariantViolation on a manifest row key that is not the fixed ten bytes, or a shard
///         payload that ends inside a record — the scan cannot produce the set at all.
template <history::HistoryTables const& Tables, history::SeekableStateStorage Storage>
bcos::task::Task<ManifestIndex> scanManifests(Storage& storage)
{
    ManifestIndex manifests;
    auto iterator = co_await bcos::storage2::range(
        storage, bcos::storage2::RANGE_SEEK, executor_v1::StateKey{Tables.manifest, ""});
    while (true)
    {
        auto row = co_await iterator.next();
        if (!row)
        {
            break;
        }
        auto const& [rowKey, rowValue] = *row;
        executor_v1::StateKeyView const rowKeyView{rowKey};
        if (rowKeyView.m_table != Tables.manifest)
        {
            break;
        }
        auto const* entry = history::detail::asStateValue(rowValue);
        if (entry == nullptr)
        {
            continue;  // an expired shard on a logical-deletion layer
        }
        if (rowKeyView.m_key.size() != history::kManifestRowKeyBytes)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "history audit: manifest row key is " +
                    std::to_string(rowKeyView.m_key.size()) + " bytes, not the fixed " +
                    std::to_string(history::kManifestRowKeyBytes)));
        }
        auto const block = static_cast<protocol::BlockNumber>(
            history::readBigEndian<history::kBlockNumberBytes>(rowKeyView.m_key));
        auto& keys = manifests[block];
        for (auto const& key : history::decodeManifestShard(entry->get()))
        {
            keys.emplace(std::string(key.begin(), key.end()), false);
        }
    }
    co_return manifests;
}

}  // namespace detail

/// Audit one reverse-history instance (spec B.10 ①-④).
///
/// Two seek-scans, one per table: the manifest table gives "which keys does each block claim to
/// have changed", the index table gives "which (key, block) pre-images actually exist". Every one
/// of the four checks is a comparison between those two sets, plus the window the caller
/// describes.
///
/// @param tip the chain's current block number.
/// @param depth this instance's retention depth — H_state or H_proof. Passed in, never read from
///        the store: it is a node-local operational parameter, and B.10 ④ is exactly the check
///        that what is on disk agrees with what the operator believes it configured.
/// @param firstHistoryBlock the earliest block this node ever wrote history for. Blocks before it
///        are not expected to have manifests, so the window starts no earlier. Defaults to
///        genesis; a node that turned history on mid-life must pass the block it turned it on at,
///        or every pre-history block is reported as a B.10 ② hole.
/// @throws MPTInvariantViolation when a row in either table is structurally undecodable — a
///         manifest row key that is not the fixed ten bytes, an index row key shorter than its
///         block field, a shard payload that ends inside a record. Those are not "inconsistencies
///         between two sets", they mean the scan cannot produce the sets at all.
template <history::HistoryTables const& Tables, history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0)
{
    HistoryAuditReport report{.tip = tip, .depth = depth};
    report.windowStart = std::max<protocol::BlockNumber>(firstHistoryBlock, tip - depth + 1);

    // ---- pass 1: the manifests. block -> (key -> has its index row been seen?) ----
    auto manifests = co_await detail::scanManifests<Tables>(storage);

    report.blocksWithManifest = manifests.size();
    for (auto const& [block, keys] : manifests)
    {
        report.manifestKeys += keys.size();
    }
    if (!manifests.empty())
    {
        report.oldestRetained = manifests.begin()->first;
        report.newestRetained = manifests.rbegin()->first;
    }

    // ---- pass 2: the index rows, checked against the manifests (B.10 ①-reverse and ③) ----
    {
        auto iterator = co_await bcos::storage2::range(
            storage, bcos::storage2::RANGE_SEEK, executor_v1::StateKey{Tables.index, ""});
        while (true)
        {
            auto row = co_await iterator.next();
            if (!row)
            {
                break;
            }
            auto const& [rowKey, rowValue] = *row;
            executor_v1::StateKeyView const rowKeyView{rowKey};
            if (rowKeyView.m_table != Tables.index)
            {
                break;
            }
            if (history::detail::asStateValue(rowValue) == nullptr)
            {
                continue;
            }
            if (rowKeyView.m_key.size() < history::kBlockNumberBytes)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "history audit: index row key is shorter than its 8-byte block field"));
            }
            ++report.indexRows;
            // Index row key = <logical key> || BE64(block); the block field is fixed width, so
            // the split is unambiguous without a length prefix (spec B.2(c)).
            auto const split = rowKeyView.m_key.size() - history::kBlockNumberBytes;
            auto const key = rowKeyView.m_key.substr(0, split);
            auto const block = static_cast<protocol::BlockNumber>(
                history::readBigEndian<history::kBlockNumberBytes>(rowKeyView.m_key.substr(split)));

            auto const manifest = manifests.find(block);
            if (manifest == manifests.end())
            {
                report.findings.push_back(HistoryFinding{.kind = HistoryFindingKind::OrphanIndexRow,
                    .block = block,
                    .key = detail::hexKey(key)});
                continue;
            }
            auto const listed = manifest->second.find(std::string(key));
            if (listed == manifest->second.end())
            {
                report.findings.push_back(
                    HistoryFinding{.kind = HistoryFindingKind::IndexRowNotInManifest,
                        .block = block,
                        .key = detail::hexKey(key)});
                continue;
            }
            listed->second = true;
        }
    }

    // ---- B.10 ①, forward: every listed key must have its index row ----
    for (auto const& [block, keys] : manifests)
    {
        for (auto const& [key, seen] : keys)
        {
            if (!seen)
            {
                report.findings.push_back(
                    HistoryFinding{.kind = HistoryFindingKind::ManifestKeyWithoutIndexRow,
                        .block = block,
                        .key = detail::hexKey(key)});
            }
        }
    }

    // ---- B.10 ②: the window must be gapless ----
    for (auto block = report.windowStart; block <= tip; ++block)
    {
        if (!manifests.contains(block))
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::MissingManifest, .block = block});
        }
    }

    // ---- B.10 ④: the retained span must be the window the depth describes ----
    // Only when something is retained: an entirely empty history is already fully reported by the
    // B.10 ② findings above, and adding a boundary finding on top would just be noise.
    if (!manifests.empty())
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

    // ---- B.10 ④, the store's own metadata: the boundary ROW against the manifests ----
    //
    // The boundary records the oldest block the store can still ANSWER FOR, and one invariant
    // covers every regime the store reaches:
    //
    //     boundary == max(0, oldestRetained - 1)
    //
    //  - after an expiry of block E: the boundary is E and E's manifest is gone, so the oldest
    //    surviving manifest is E + 1 (ReverseHistoryStore::expire, RetentionBoundary::Advance);
    //  - on a chain younger than the window, nothing has expired and the boundary is the seed
    //    max(0, firstRecorded - 1), with firstRecorded the oldest manifest
    //    (HistoryCommit.h::seedRetentionBoundary);
    //  - a Keep-rollback trims manifests off the TOP and leaves the boundary alone, so the oldest
    //    is untouched either way.
    //
    // The clamp matters at genesis: a store seeded at block 0 writes boundary 0 while its oldest
    // manifest is also 0, and an unclamped `oldest - 1` would read that healthy store as broken.
    //
    // Judged only when manifests exist. A store with none has no oldest to compare against, and
    // whether that is a fault at all is already answered by the B.10 ② findings above.
    report.retentionBoundary =
        co_await history::ReverseHistoryStore<Tables>::retentionBoundary(storage);
    if (!manifests.empty())
    {
        auto const expectedBoundary = std::max<protocol::BlockNumber>(0, report.oldestRetained - 1);
        if (!report.retentionBoundary)
        {
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryRowMismatch,
                    .block = expectedBoundary,
                    .detail = "this store holds manifests but has no retention-boundary row; "
                              "every serving query reads that row to decide whether a height was "
                              "ever recorded, and an absent one means the store cannot say"});
        }
        else if (*report.retentionBoundary != expectedBoundary)
        {
            // The two directions are not equally bad, and the report says which one this is.
            auto const detail =
                *report.retentionBoundary < expectedBoundary ?
                    std::string("the boundary claims heights this store no longer holds (row " +
                                std::to_string(*report.retentionBoundary) + ", oldest manifest " +
                                std::to_string(report.oldestRetained) +
                                "); a historical read below the oldest manifest seeks past every "
                                "row and answers with TODAY's value under an old block number") :
                    std::string("the boundary is above the oldest manifest (row " +
                                std::to_string(*report.retentionBoundary) + ", oldest manifest " +
                                std::to_string(report.oldestRetained) +
                                "); those manifests are unreachable — refused by the window guard "
                                "— so they are dead weight rather than a wrong answer");
            report.findings.push_back(
                HistoryFinding{.kind = HistoryFindingKind::RetentionBoundaryRowMismatch,
                    .block = *report.retentionBoundary,
                    .detail = detail});
        }
    }

    co_return report;
}

/// B.10 over the state history, retained for H_state blocks.
template <history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditStateHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0)
{
    return auditHistory<history::kStateHistory>(storage, tip, depth, firstHistoryBlock);
}

/// B.10 over the trie-node history, retained for H_proof blocks.
template <history::SeekableStateStorage Storage>
bcos::task::Task<HistoryAuditReport> auditTrieHistory(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber depth, protocol::BlockNumber firstHistoryBlock = 0)
{
    return auditHistory<history::kTrieHistory>(storage, tip, depth, firstHistoryBlock);
}

}  // namespace bcos::ledger::mpt::audit
