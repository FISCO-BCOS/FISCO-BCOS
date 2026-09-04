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
 * @file PruneMetadata.h
 * @brief Key layout and value codec for the MPT pruning metadata rows (spec §4.8):
 *        the refcount table, the pending-delete queue table and the watermark row
 */
#pragma once

#include "Errors.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace bcos::ledger::mpt
{

/// The pruning metadata lives in three sys tables of ordinary state rows, written into the
/// block's prewriteStorage by the commit flow (CommitObserver::coPreparePruneRows) so they land
/// in the SAME WriteBatch as the block data:
///
///   row          | key                                             | value
///   -------------+-------------------------------------------------+---------------------------
///   refcount     | /sys/mpt_prune_ref/   : 32B node hash           | RLP(count, pendingDeleteAt?)
///   delete queue | /sys/mpt_prune_queue/ : BE-u64 targetBlock +    | empty (presence = queued;
///                |                     32B node hash               |  prefix-scans by targetBlock)
///   watermark    | /sys/mpt_prune_meta/  : "watermark"             | 8-byte BE-u64, the highest
///                |                                                 |  block number whose pruning
///                |                                                 |  metadata is persisted
///   seed marker  | /sys/mpt_prune_meta/  : "seeded"                | empty; present only when the
///                |                                                 |  genesis refcounts were
///                |                                                 |  seeded (scenario B / L2)
///
/// Table names carry a trailing '/' like the "/mpt/" node table (KeyPrefixes.h) and must never
/// contain ':' — StateKeyResolver splits a physical key at its FIRST colon.
inline constexpr std::string_view kPruneRefTable = "/sys/mpt_prune_ref/";
inline constexpr std::string_view kPruneQueueTable = "/sys/mpt_prune_queue/";
inline constexpr std::string_view kPruneMetaTable = "/sys/mpt_prune_meta/";
inline constexpr std::string_view kWatermarkRowKey = "watermark";
inline constexpr std::string_view kSeedMarkerRowKey = "seeded";

static_assert(kPruneRefTable.find(':') == std::string_view::npos &&
                  kPruneQueueTable.find(':') == std::string_view::npos &&
                  kPruneMetaTable.find(':') == std::string_view::npos,
    "pruning metadata table names must not contain ':' — StateKeyResolver splits a physical key "
    "at its FIRST colon");

/// One refcount row, decoded: how many trie versions reference the node, and — when the count
/// sits at 0 — the block number at which the node becomes deletable (nullopt = not queued).
struct PruneRefCount
{
    uint64_t count{0};
    std::optional<uint64_t> pendingDeleteAt{};

    friend bool operator==(PruneRefCount const&, PruneRefCount const&) = default;
};

/// The refcount row of one node hash.
bcos::executor_v1::StateKey pruneRefKey(bcos::h256 const& hash);

/// The queue row scheduling @p hash for deletion at @p targetBlock. The key part is
/// 8-byte big-endian targetBlock followed by the 32 raw digest bytes, so ordered iteration over
/// kPruneQueueTable visits rows in ascending targetBlock order and a prefix read of the first 8
/// bytes is the deletion deadline.
bcos::executor_v1::StateKey pruneQueueKey(uint64_t targetBlock, bcos::h256 const& hash);

/// The inverse of pruneQueueKey's key part. @p keyPart is the StateKey's key half (already
/// stripped of the table); 40 bytes exactly. @throws MPTDecodeError on any other length.
std::pair<uint64_t, bcos::h256> decodeQueueKeyPart(std::string_view keyPart);

/// The single watermark row: highest block number whose pruning metadata is on disk.
bcos::executor_v1::StateKey watermarkKey();

/// The seed marker row: present iff the genesis trie nodes got their refcount rows at genesis
/// (scenario B / L2 chains). MPTPruner::init's startup guard requires it on an L2 chain whose
/// genesis predates seeding.
bcos::executor_v1::StateKey seedMarkerKey();

/// RLP list [count, pendingDeleteAt?] — a disengaged pendingDeleteAt is simply omitted from the
/// tail of the list (RLP optional convention), so a live node's row is [count] alone.
bcos::bytes encodeRefCount(PruneRefCount const& refCount);

/// The inverse of encodeRefCount. @throws MPTDecodeError on malformed input (bad RLP, trailing
/// bytes) — a corrupted metadata row fails the commit loudly rather than miscounting silently.
PruneRefCount decodeRefCount(bcos::bytesConstRef encoded);

/// The watermark value: exactly 8 bytes, big-endian.
bcos::bytes encodeWatermark(uint64_t blockNumber);

/// The inverse of encodeWatermark. @throws MPTDecodeError on any length other than 8.
uint64_t decodeWatermark(bcos::bytesConstRef encoded);

/// Genesis seeding (scenario B / L2): one refcount row per genesis trie node with the node's
/// EMISSION multiplicity (GenesisStateTrie::nodeCounts — a node shared by K genesis sub-tries
/// is referenced K times and must start at count K, or the first referencing account's rebuild
/// would drop it to 0 and schedule a live node for deletion), plus the seed marker the startup
/// guard checks. Written with the genesis state rows, before any block commits. Duck-typed on
/// storage2::writeOne — Ledger::buildGenesisBlock calls this over its legacy StorageInterface
/// (via LegacyStorageMethods) and tests over a bare storage2 backend.
template <class Storage>
bcos::task::Task<void> writePruneSeedRows(
    Storage& storage, std::unordered_map<bcos::h256, uint64_t> const& nodeCounts)
{
    for (auto const& [hash, count] : nodeCounts)
    {
        bcos::storage::Entry refEntry;
        refEntry.set(encodeRefCount(PruneRefCount{.count = count}));
        co_await bcos::storage2::writeOne(storage, pruneRefKey(hash), std::move(refEntry));
    }
    bcos::storage::Entry markerEntry;
    markerEntry.set(std::string{});
    co_await bcos::storage2::writeOne(storage, seedMarkerKey(), std::move(markerEntry));
}

}  // namespace bcos::ledger::mpt
