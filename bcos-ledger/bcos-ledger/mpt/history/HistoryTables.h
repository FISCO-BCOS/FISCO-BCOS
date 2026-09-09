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
 * @file HistoryTables.h
 * @brief The four state tables the reverse history lives in (layout spec §1.1)
 */
#pragma once

#include <string_view>

namespace bcos::ledger::mpt::history
{

/// The pair of state tables one reverse-history instance owns.
///
/// The optimized layout (spec §1.1) keeps the old values themselves inside the per-block shards
/// and answers point queries from an in-memory index (HistoryIndex.h), so the (key, block)-ordered
/// index TABLE the earlier layout needed is gone: one sort order now serves the disk, and the
/// second sort order lives in RAM where it costs no writes and no expiry point-deletes.
///
/// ```text
/// shard     StateKey{"/mpths/" | "/mptth/", <BE64 block>}                 -> meta,  41 bytes
/// shard     StateKey{"/mpths/" | "/mptth/", <BE64 block> <BE16 shard>}    -> records
/// boundary  StateKey{"/mptsr/" | "/mpttr/", "boundary"}                   -> <BE64 block>
/// ```
///
/// Meta rows and shard rows share the `shard` table on purpose: an 8-byte key sorts before every
/// 10-byte key that starts with the same 8 bytes, so ONE `RANGE_SEEK(BE64(block))` yields the
/// block's meta row and then its shards in ordinal order — which is exactly what both the
/// whole-block read and the startup rebuild walk. The two kinds are told apart by key length
/// (isMetaRowKey / isShardRowKey), never by content.
///
/// The boundary table holds ONE row, the retention boundary (spec §13's "保留边界与记录的 H_state
/// / H_proof 元数据一致"). Its value is the OLDEST block this store can still ANSWER FOR:
/// everything below it has been expired. Answerable, not "whose own rows survive" — a query for B
/// is served from the first change AFTER B, so dropping block E leaves E answerable from E+1's
/// rows and takes E-1 away. It only ever grows; expired data does not come back. It is written in
/// the SAME batch as the expiry deletes that advance it, so no crash can leave a boundary that
/// disagrees with the rows.
///
/// **Only expiry from the BOTTOM moves it** (ReverseHistoryStore::RetentionBoundary). Two callers
/// discard a block's history, from opposite ends of the chain:
///
///  - the commit path drops E = N - H, the block leaving the window from below, so E becomes the
///    oldest block the store can answer for — `RetentionBoundary::Advance`;
///  - an operational rollback walks the tip DOWNWARDS, reverse-applying block N and then dropping
///    N's record. Discarding the newest block does not change which is the oldest, and advancing
///    here would break the walk's own next step: rollback reads at N-2 immediately after
///    discarding N, and a boundary at N refuses it — `RetentionBoundary::Keep`, which is the
///    default so that only the one caller that knows better has to say so.
///
/// It lives in its own TABLE rather than in a reserved row of the shard table, and under this
/// layout that separation is load-bearing rather than merely tidy. Meta rows and shard rows are
/// told apart by key LENGTH — and the boundary row key `"boundary"` is eight bytes, exactly a meta
/// row key's length. Sharing the table would make it indistinguishable from the meta row of block
/// 0x626F756E64617279 to every walker, with no signal. The rebuild also reads the boundary to
/// decide where to START its shard walk, so the boundary must not be inside the range being
/// walked.
struct HistoryTables
{
    std::string_view shard;
    std::string_view boundary;
};

/// State history: the pre-images of ordinary state rows, retained for H_state blocks.
inline constexpr HistoryTables kStateHistory{.shard = "/mpths/", .boundary = "/mptsr/"};

/// Trie history: the pre-images of path-addressed trie-node rows, retained for H_proof blocks.
/// Same mechanism, second instantiation (spec B.8).
inline constexpr HistoryTables kTrieHistory{.shard = "/mptth/", .boundary = "/mpttr/"};

/// **Block-history coverage contract** — what a consumer (PR-D's rollback and audit) may assume.
///
/// A Meta row exists for block N in store S **iff** S's retention depth was > 0 when N committed
/// AND N built an MPT delta. Both commit paths gate on exactly that, and a block that meets both
/// conditions but changed nothing still gets `Meta{shardCount = 1, recordCount = 0}` plus an empty
/// shard 0 (spec B.10 ②), so "no Meta row" is never ambiguous with "changed nothing".
///
/// The converse is what matters to a consumer: a chain has blocks with NO Meta row in either store
/// — every pre-MPT block of a scenario-A chain, and every block committed while the depth was 0.
/// Empty records are deliberately NOT written for them: a Meta row asserts "this block's
/// pre-images are recorded here", and for a block whose pre-images were never captured that
/// assertion would be false, which is worse than its absence. A rollback or an audit that spans
/// such a block therefore has no data to work from and must REFUSE rather than treat the gap as an
/// empty diff.
inline constexpr std::string_view kBlockHistoryCoverageContract =
    "a Meta row exists for block N in store S iff S's depth was > 0 and N built an MPT delta";

/// A table name must not contain ':': StateKeyResolver splits a physical key at its FIRST colon to
/// rebuild the (table, rowKey) pair, so a colon in the table name would corrupt the split. History
/// row keys DO contain arbitrary bytes (0x3A among them) — that is safe precisely because every
/// colon in them sits after the first one. Same rule and reasoning as
/// bcos-storage/KeyPrefixes.h:43.
static_assert(kStateHistory.shard.find(':') == std::string_view::npos &&
                  kStateHistory.boundary.find(':') == std::string_view::npos &&
                  kTrieHistory.shard.find(':') == std::string_view::npos &&
                  kTrieHistory.boundary.find(':') == std::string_view::npos,
    "a history table name must not contain ':'");

/// The four tables must stay distinct: rows are located by seeking into a table and walking
/// forward until the table changes, so a shared name would let one instance's walk consume the
/// other's rows — and the boundary row must be unreachable from either walk. All four names are
/// the same length, so distinctness also rules out one being a prefix of another.
static_assert(kStateHistory.shard.size() == kStateHistory.boundary.size() &&
                  kStateHistory.shard.size() == kTrieHistory.shard.size() &&
                  kStateHistory.shard.size() == kTrieHistory.boundary.size(),
    "the four history table names are asserted distinct below on the strength of being equal "
    "length; if that stops holding, the prefix case needs its own check");
static_assert(kStateHistory.shard != kStateHistory.boundary &&
                  kStateHistory.shard != kTrieHistory.shard &&
                  kStateHistory.shard != kTrieHistory.boundary &&
                  kStateHistory.boundary != kTrieHistory.shard &&
                  kStateHistory.boundary != kTrieHistory.boundary &&
                  kTrieHistory.shard != kTrieHistory.boundary,
    "the four history table names must be distinct");

}  // namespace bcos::ledger::mpt::history
