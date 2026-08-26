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
 * @file HashBuilder.h
 * @brief Stateless canonical-MPT construction: computeTrieRoot cores + the commitTrie entry
 *        point (spec §5.3, §5.4)
 */
#pragma once

#include "Constants.h"
#include "TrieMerge.h"
// Named directly for the keccak hasher commitTrie constructs (also reachable via TrieMerge.h).
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <map>
#include <optional>
#include <range/v3/range.hpp>
#include <range/v3/view/transform.hpp>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// Result of a stateless trie build: the 32-byte root plus the hash-keyed RLP encodings of every
/// node that must be persisted (the hash-kind nodes produced during the build).
struct TrieBuildResult
{
    bcos::h256 root;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
};

/// Stateless, reentrant entry point: build one canonical MPT from an ordered keyHash -> value map
/// and return {root, newNodes}. The input is std::map specifically: its type already pins down
/// everything the build relies on — ascending iteration (red-black invariant), the canonical order
/// (default std::less<h256> == 32-byte lexicographic == 64-nibble path order), and key uniqueness
/// (not a multimap). So there is no sort, no is_sorted check, no dup handling — just one O(n) walk.
/// Touches no object state and no storage, so independent inputs may be built concurrently
/// (coarse-grained parallelism across tries). Internally single-threaded.
/// @note Synchronous (returns a value, not a Task), unlike the storage-reading commitTrie().
TrieBuildResult computeTrieRoot(std::map<bcos::h256, bcos::bytes> const& entries);

/// Stateless core over already-sorted, unique (keyHash, value-view) entries. commitTrie()'s
/// from-empty path calls this after resolving its change-set; the header cannot pull in the build
/// internals, so this free function is the seam. Callers guarantee ascending, deduplicated keys.
TrieBuildResult computeTrieRootFromSorted(
    std::span<std::pair<bcos::h256, bcos::bytesConstRef> const> sortedEntries);

/// Stateless canonical-MPT root over RAW (non-hashed) byte keys — the Ethereum "non-secure"
/// tries (the block header's transaction / receipt / withdrawal tries), whose keys are
/// variable-length byte strings (RLP-encoded indices), not keccak digests.
///
/// The build core is shared with the 64-nibble path: any-length keys are split into nibbles the
/// same way, and — unlike the fixed-length invariant of computeTrieRootFromSorted — a key may
/// terminate exactly where another continues (a proper-prefix key), which is stored as the
/// BranchNode's own value. The RLP-encoded-index keys Ethereum actually uses are prefix-free, so
/// that case never fires there, but trietest.json exercises it, so it is supported here.
///
/// @param sortedEntries  Unique keys, sorted ascending by raw key bytes (lexicographic == nibble
///                       path order — note this is NOT numeric index order for RLP-encoded
///                       indices: rlp(0)=0x80 sorts after rlp(1)=0x01).
/// @return {root, newNodes} — root is emptyRootHash() for an empty input.
TrieBuildResult computeTrieRootFromRawKeys(
    std::span<std::pair<bcos::bytesConstRef, bcos::bytesConstRef> const> sortedEntries);

/// Non-secure (variable-length key) build entry: computes a canonical MPT over (key, value) pairs
/// where @p key is an arbitrary-length byte string (each byte → 2 nibbles via bytesToNibbles),
/// NOT the 32-byte keccak-hashed secure-trie key `computeTrieRoot` assumes. Used for list tries
/// whose keys are short RLP encodings (e.g. the OP receipts-root: key = rlp(index), 1–4 bytes).
///
/// Contract (differs from computeTrieRoot's 64-nibble assumption): the caller MUST guarantee no
/// key is a prefix of another (a variable-length key may terminate inside a branch node, which
/// this build does not support — the 64-nibble case never does). The OP receipts-root keys
/// rlp(0..N) satisfy this by construction. Keys are also assumed distinct. The input does NOT
/// need to be pre-sorted: the build sorts defensively by nibble-path order (byte lexicographic
/// == nibble-path order) — a fix for the OP callers passing index-order entries, which produced
/// a malformed trie once 2-byte keys (rlp(128..)) appeared alongside rlp(0) (W6 L2
/// isthmus_big_block_130tx).
TrieBuildResult computeTrieRootVarKey(std::span<std::pair<bcos::bytes, bcos::bytes> const> entries);

/// Apply one change-set over a prior trie version and return the complete node delta — the single
/// stateless entry point every trie producer commits through (spec §5.4, Revision 2026-07-09b:
/// the stateful put/remove-accumulating HashBuilder class this replaces held no state that
/// outlived a single commit pass, so the class boundary bought nothing).
///
/// Keys are full 32-byte hashes (the "secure trie" key transform happens upstream): their
/// 64-nibble paths are all the same length, so no key ever terminates inside a branch. Because
/// 32-byte lexicographic order equals 64-nibble path order, the std::map already yields paths in
/// canonical sorted order. A nullopt mapped value records a delete (a no-op when the key is
/// absent from the prior trie).
///
/// Two build paths, selected by the prior root:
///  - priorRoot == emptyRootHash(): from-empty build over the stateless computeTrieRootFromSorted
///    core (deletes resolve to no-ops); obsoletedNodes is empty by construction.
///  - otherwise: path-level incremental rebuild via mergeTrie() (spec §5.3 path 1) — only nodes
///    on changed key paths are read through @p storage; untouched subtrees are re-referenced by
///    hash, unread. A missing referenced node throws MPTInvariantViolation.
///
/// commitTrie only READS @p storage. Flushing result.newNodes is the caller's job (MPTBuilder
/// batches one writeSome per block) — a per-commit flush here would turn N per-account commits
/// into N storage round-trips and would write nodes a later commit of the same block may already
/// obsolete.
///
/// Keccak-pinned: the from-empty stateless core is not yet hasher-parameterized, so a non-keccak
/// instantiation would silently mix hash functions between the two paths.
///
/// @todo HasherT — parameterize computeTrieRootFromSorted, accountKeyHash and Account's default
/// storageRoot/codeHash together with this function; doing a subset silently mixes hash
/// functions, which is exactly the failure this pinning exists to prevent.
///
/// [[nodiscard]] on purpose: the result carries newNodes, which only exist in memory until the
/// caller passes them to flushTrieNodes. Dropping the return value loses the block's nodes with
/// no diagnostic — the new root would reference nodes that were never written.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
[[nodiscard]] bcos::task::Task<TrieMergeResult> commitTrie(Storage& storage, bcos::h256 priorRoot,
    std::map<bcos::h256, std::optional<bcos::bytes>> const& changes)
{
    if (changes.empty())
    {
        co_return TrieMergeResult{.root = priorRoot, .newNodes = {}, .obsoletedNodes = {}};
    }
    if (priorRoot != emptyRootHash())
    {
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
        co_return co_await mergeTrie(storage, priorRoot, changes, hasher);
    }

    // From-empty: deletes are no-ops; the map's survivors stay sorted.
    std::vector<std::pair<bcos::h256, bcos::bytesConstRef>> survivors;
    survivors.reserve(changes.size());
    for (auto const& [key, maybeValue] : changes)
    {
        if (maybeValue.has_value())
        {
            survivors.emplace_back(key, bcos::ref(*maybeValue));
        }
    }
    auto built = computeTrieRootFromSorted(survivors);
    co_return TrieMergeResult{
        .root = built.root, .newNodes = std::move(built.newNodes), .obsoletedNodes = {}};
}

/// Batch-write @p nodes — any input range of (hash → raw RLP) pairs — into @p storage in one
/// writeSome round-trip, the flush counterpart of commitTrie for the caller that owns node
/// persistence (MPTBuilder flushes its aggregated MPTDeltaLayer.newNodes once per block; tests
/// flush per build). @p nodes is read through a reference view — elements are copied into
/// storage, never moved from: the caller's delta must stay intact for the commit flow and
/// CommitObserver.
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage>
bcos::task::Task<void> flushTrieNodes(Storage& storage, ::ranges::input_range auto const& nodes)
{
    co_await bcos::storage2::writeSome(
        storage, nodes | ::ranges::views::transform(
                             [](auto const& node) { return std::tie(node.first, node.second); }));
}

}  // namespace bcos::ledger::mpt
